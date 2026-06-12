# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence
from typing import final

from typing_extensions import override

from .. import _events, _streams
from .._bucket_sources import (
    BucketSource,
    PyBucketSource,
    RecyclingBucketSource,
)
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _size_type,
    _uint64_type,
)
from .._events import BucketEvent, EventType, WarningEvent
from .._graph import Graph, Subgraph
from .._node import Node, _RelayNode
from .._param import Param
from ._batching import Batch, Unbatch
from ._common import (
    _bucket_source_or_default,
    _check_events_subset_of,
)
from ._stopping import Stop, StopWithError


@final
class ExtractBucket(_RelayNode):
    """Processor that extracts the bucket carried by a bucket-carrying event.

    Pulls the ``data_bucket`` field out of each ``event_type`` and emits it as
    a `BucketEvent`. Use this to obtain the result of `Histogram` or
    `ScanHistograms` as a bare NumPy array. Alternatively, bucket-carrying
    events can be sent to a Python sink directly, where they are delivered as
    `EventInstance` values; prefer `ExtractBucket` when only the array (not
    the event wrapper) is of interest.

    Parameters
    ----------
    event_type : EventType
        The bucket-carrying event type to extract from. The input event set
        must consist only of this type, which must carry a ``data_bucket``.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: emit the carried bucket as a
      `BucketEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::extract_bucket`
        The underlying C++ factory function.
    """

    def __init__(self, event_type: EventType) -> None:
        if not hasattr(event_type, "_data_bucket_element_event_type"):
            raise ValueError(
                f"{event_type} does not carry an extractable bucket"
            )
        self._event_type = event_type

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (self._event_type,), self.__class__.__name__
        )
        element = self._event_type._data_bucket_element_event_type()  # type: ignore[attr-defined]
        return (BucketEvent(element),)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::extract_bucket<{self._event_type._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class ReadBinaryStream(_RelayNode):
    """Source processor that reads events from a binary input stream.

    The stream must contain a contiguous array of ``event_type``, which
    must be a trivially-typed event. Events are read from the stream in
    batches and placed into buckets obtained from the buffer provider.
    Each completed read is emitted as a `BucketEvent` of ``event_type``.
    The read size is governed by ``read_granularity_bytes`` and by the
    size of ``event_type``. The first read may be shortened to align
    subsequent read offsets to the granularity, and the last read may
    be shortened to avoid exceeding ``max_length``.

    Parameters
    ----------
    event_type : EventType
        Element type stored in the stream. Must be a trivially-typed
        event.
    stream : InputStream
        The input stream to read from (for example a
        `BinaryFileInputStream`).
    max_length : int or Param[int] or None
        Maximum number of bytes to read. ``None`` (the default) means
        read to end of stream. Should be a multiple of the size of
        ``event_type`` for clean truncation.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets used to hold each read. If ``None``, a default
        `RecyclingBucketSource` for ``event_type`` is used. Must be able
        to circulate at least 2 buckets without blocking. A runtime
        `Param` of type `PyBucketSource` binds a Python bucket source at
        execution time.
    read_granularity_bytes : int or Param[int]
        Minimum read size in bytes. Defaults to ``65536``. Larger reads
        have less per-byte overhead but may pollute CPU caches; try
        different powers of two and measure.

    Notes
    -----
    Events handled:

    - This processor has no input events; it is a source.
    - Emits `BucketEvent` of ``event_type`` for each completed read.
    - End of input: pass through. Raises an exception (corresponding to
      C++ ``input_output_error``) on a stream read error; emits a
      `WarningEvent` if the stream ends with fewer than
      ``sizeof(event_type)`` bytes remaining.

    See Also
    --------
    :cpp:`tcspc::read_binary_stream`
        The underlying C++ factory function.
    :py:obj:`read_events_from_binary_file`
        Higher-level convenience that reads a binary file and emits individual
        events (rather than byte buckets).
    """

    def __init__(
        self,
        event_type: EventType,
        stream: _streams.InputStream,
        max_length: int | Param[int] | None = None,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
        read_granularity_bytes: int | Param[int] = 65536,
    ):
        self._event_type = event_type
        self._stream = stream
        self._maxlen = max_length
        self._bucket_source = _bucket_source_or_default(
            event_type, buffer_provider
        )
        self._granularity = read_granularity_bytes

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(input_event_set, (), self.__class__.__name__)
        return (BucketEvent(self._event_type), WarningEvent())

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._maxlen, Param):
            params.append((self._maxlen, _uint64_type))
        if isinstance(self._granularity, Param):
            params.append((self._granularity, _size_type))
        params.extend(self._stream._parameters())
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        maxlen = (
            "std::numeric_limits<tcspc::u64>::max()"
            if self._maxlen is None
            else gencontext.u64_expression(self._maxlen)
        )
        granularity = gencontext.size_t_expression(self._granularity)
        return _CppExpression(
            f"""\
            tcspc::read_binary_stream<{self._event_type._cpp_type_name()}>(
                {self._stream._cpp_expression(gencontext)},
                tcspc::arg::max_length<tcspc::u64>{{{maxlen}}},
                {self._bucket_source._cpp_expression(gencontext)},
                tcspc::arg::granularity<std::size_t>{{{granularity}}},
                {downstream}
            )"""
        )


@final
class BatchFromBytes(_RelayNode):
    """Processor that copies batches of bytes into batches of typed events.

    Input is a `BucketEvent` of an internal byte event type (``std::byte``).
    The processor copies the incoming bytes into a fresh `BucketEvent` of
    ``event_type``, drawing storage from the buffer provider. Any trailing
    bytes that do not make up a whole ``event_type`` are buffered and
    combined with subsequent input.

    Parameters
    ----------
    event_type : EventType
        Element type of the output buckets. Must be a trivially-typed
        event.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets used to hold each output batch. If ``None``, a
        default `RecyclingBucketSource` for ``event_type`` is used. A runtime
        `Param` of type `PyBucketSource` binds a Python bucket source at
        execution time.

    Notes
    -----
    Events handled:

    - `BucketEvent` of byte event type: copy up to object boundary as
      `BucketEvent` of ``event_type``; emit.
    - All other event types: rejected at graph build time.
    - End of input: pass through. Raises an exception if any unconsumed
      bytes remain.

    See Also
    --------
    :cpp:`tcspc::batch_from_bytes`
        The underlying C++ factory function.
    :py:obj:`UnbatchFromBytes`
        The inverse: emit typed events one at a time from byte batches.
    :py:obj:`ViewAsBytes`
        View bucketed events as their in-memory bytes.
    """

    def __init__(
        self,
        event_type: EventType,
        *,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
    ) -> None:
        self._event_type = event_type
        self._byte_event_type = _events.BucketEvent(_events._ByteEvent())
        self._bucket_source = _bucket_source_or_default(
            event_type, buffer_provider
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return tuple(self._bucket_source._parameters())

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (self._byte_event_type,),
            self.__class__.__name__,
        )
        return (_events.BucketEvent(self._event_type),)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::batch_from_bytes<{self._event_type._cpp_type_name()}>(
                {self._bucket_source._cpp_expression(gencontext)},
                {downstream}
            )"""
        )


@final
class UnbatchFromBytes(_RelayNode):
    """Processor that emits typed events one-at-a-time from batches of bytes.

    Input is a `BucketEvent` of bytes. The incoming bytes are interpreted
    as a contiguous stream of ``event_type`` and emitted individually
    (after copying for alignment if needed). Trailing bytes that do not
    form a whole ``event_type`` are buffered and combined with subsequent
    input.

    Parameters
    ----------
    event_type : EventType
        Element type emitted. Must be a trivially-typed event.

    Notes
    -----
    Events handled:

    - `BucketEvent` of byte event type: each contained event is emitted
      one at a time.
    - All other event types: rejected at graph build time.
    - End of input: pass through. Raises an exception if any unconsumed
      bytes remain.

    See Also
    --------
    :cpp:`tcspc::unbatch_from_bytes`
        The underlying C++ factory function.
    :py:obj:`BatchFromBytes`
        The inverse: copy batches of bytes into batches of typed events.
    """

    def __init__(self, event_type: EventType) -> None:
        self._event_type = event_type
        self._byte_event_type = _events.BucketEvent(_events._ByteEvent())

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (self._byte_event_type,),
            self.__class__.__name__,
        )
        return (self._event_type,)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::unbatch_from_bytes<{self._event_type._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class ViewAsBytes(_RelayNode):
    """Processor that emits the in-memory bytes of bucketed events.

    For each incoming `BucketEvent` of ``event_type``, the underlying
    storage is viewed (without copying) as a `BucketEvent` of bytes and
    emitted.

    Parameters
    ----------
    event_type : EventType
        Element type of the input buckets. Must be trivially-typed.

    Notes
    -----
    Events handled:

    - `BucketEvent` of ``event_type``: emit its storage as `BucketEvent`
      of bytes (no copy).
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::view_as_bytes`
        The underlying C++ factory function.
    :py:obj:`BatchFromBytes`
        Copy batches of bytes into batches of typed events.
    """

    def __init__(self, event_type: EventType) -> None:
        self._event_type = event_type
        self._byte_event_type = _events.BucketEvent(_events._ByteEvent())

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.BucketEvent(self._event_type),),
            self.__class__.__name__,
        )
        return (self._byte_event_type,)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::view_as_bytes(
                {downstream}
            )"""
        )


@final
class WriteBinaryStream(Node):
    """Sink processor that writes raw bytes to a binary output stream.

    Input is a `BucketEvent` of bytes; the contained bytes are written to
    the output stream. To write typed events, precede this processor with
    `ViewAsBytes` (and usually `Batch`) to convert bucketed events to their
    in-memory bytes. This processor is a sink: it has no outputs.

    Parameters
    ----------
    stream : OutputStream
        The output stream to write to (for example a
        `BinaryFileOutputStream`).
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of byte buckets used to coalesce writes. If ``None``, a
        default `RecyclingBucketSource` for bytes is used. A runtime
        `Param` of type `PyBucketSource` binds a Python bucket source at
        execution time.
    write_granularity_bytes : int or Param[int]
        Minimum write size in bytes. Defaults to ``65536``. Larger writes
        have less per-byte overhead but may pollute CPU caches; try
        different powers of two and measure.

    Notes
    -----
    Events handled:

    - `BucketEvent` of byte event type: write its bytes to the stream.
    - All other event types: rejected at graph build time.
    - End of input: flush the stream. Raises an exception (corresponding
      to C++ ``input_output_error``) on a stream write error.

    See Also
    --------
    :cpp:`tcspc::write_binary_stream`
        The underlying C++ factory function.
    :py:obj:`ViewAsBytes`
        View bucketed events as their in-memory bytes to feed this sink.
    :py:obj:`ReadBinaryStream`
        The inverse: read events from a binary input stream.
    :py:obj:`write_events_to_binary_file`
        Higher-level convenience that accepts individual events and writes them
        to a binary file (batching and byte conversion handled internally).
    """

    def __init__(
        self,
        stream: _streams.OutputStream,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
        write_granularity_bytes: int | Param[int] = 65536,
    ) -> None:
        super().__init__(output=())
        self._stream = stream
        self._byte_event_type = _events.BucketEvent(_events._ByteEvent())
        self._bucket_source = _bucket_source_or_default(
            _events._ByteEvent(), buffer_provider
        )
        self._granularity = write_granularity_bytes

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 1:
            raise ValueError(
                f"wrong number of inputs (1 expected, {len(input_event_sets)} found)"
            )
        _check_events_subset_of(
            input_event_sets[0],
            (self._byte_event_type,),
            self.__class__.__name__,
        )
        return ()

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._granularity, Param):
            params.append((self._granularity, _size_type))
        params.extend(self._stream._parameters())
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        granularity = gencontext.size_t_expression(self._granularity)
        return _CppExpression(
            f"""\
            tcspc::write_binary_stream(
                {self._stream._cpp_expression(gencontext)},
                {self._bucket_source._cpp_expression(gencontext)},
                tcspc::arg::granularity<std::size_t>{{{granularity}}}
            )"""
        )


def read_events_from_binary_file(
    event_type: EventType,
    filename: str | Param[str],
    *,
    start_offset: int | Param[int] = 0,
    max_length: int | Param[int] | None = None,
    read_granularity_bytes: int | Param[int] = 65536,
    stop_normally_on_error: bool = False,
) -> Node:
    """Build a subgraph that reads events from a binary file and emits them one at a time.

    This is a thin convenience composed of `ReadBinaryStream` followed by
    `Stop` or `StopWithError` (triggered on `WarningEvent`) and `Unbatch`.
    The resulting subgraph has one input named ``"input"`` (no input events
    expected; the subgraph acts as a source) and one output named
    ``"output"`` that emits individual events of ``event_type``.

    Parameters
    ----------
    event_type : EventType
        Element type stored in the file. Must be a trivially-typed event.
    filename : str or Param[str]
        Path to the input file. May be supplied as a runtime `Param`.
    start_offset : int or Param[int]
        Byte offset within the file at which to start reading. Defaults to
        ``0``.
    max_length : int or Param[int] or None
        Maximum number of bytes to read. ``None`` (the default) means read
        to end of file. Should be a multiple of the size of ``event_type``.
    read_granularity_bytes : int or Param[int]
        Read chunk size in bytes. Defaults to ``65536``. Larger reads have
        less overhead but may pollute CPU caches.
    stop_normally_on_error : bool
        If ``True``, treat read errors as a normal end of input, terminating
        the stream cleanly. If ``False`` (the default), a read error raises
        an exception.

    Returns
    -------
    Subgraph
        A subgraph with input ``"input"`` and output ``"output"`` that
        emits individual events of ``event_type``.

    See Also
    --------
    :py:obj:`ReadBinaryStream`
    :py:obj:`Unbatch`
    :py:obj:`Stop`
    :py:obj:`StopWithError`
    """
    g = Graph()
    g.add_chain(
        [
            (
                "reader",
                ReadBinaryStream(
                    event_type,
                    _streams.BinaryFileInputStream(
                        filename, start_offset=start_offset
                    ),
                    max_length,
                    RecyclingBucketSource(event_type),
                    read_granularity_bytes,
                ),
            ),
            (
                Stop((WarningEvent(),), "error reading input")
                if stop_normally_on_error
                else StopWithError(
                    (WarningEvent(),),
                    "error reading input",
                )
            ),
            (
                "unbatcher",
                Unbatch(BucketEvent(event_type)),
            ),
        ]
    )
    return Subgraph(
        g,
        input_map={"input": ("reader", "input")},
        output_map={"output": ("unbatcher", "output")},
    )


def write_events_to_binary_file(
    event_type: EventType,
    filename: str | Param[str],
    *,
    truncate: bool | Param[bool] = False,
    append: bool | Param[bool] = False,
    batch_size: int | Param[int] | None = None,
    write_granularity_bytes: int | Param[int] = 65536,
) -> Node:
    """Build a subgraph that writes events to a binary file.

    This is a thin convenience composed of `Batch`, `ViewAsBytes`, and
    `WriteBinaryStream` writing to a `BinaryFileOutputStream`. The resulting
    subgraph has one input named ``"input"`` that accepts individual events of
    ``event_type`` and no outputs (it is a sink).

    Parameters
    ----------
    event_type : EventType
        Element type to write to the file. Must be a trivially-typed event.
    filename : str or Param[str]
        Path to the output file. May be supplied as a runtime `Param`.
    truncate : bool or Param[bool]
        If true, truncate the file if it already exists. Defaults to
        ``False``.
    append : bool or Param[bool]
        If true, append to the file if it already exists. Defaults to
        ``False``.
    batch_size : int or Param[int] or None
        Number of events per batch. ``None`` (the default) uses the `Batch`
        default.
    write_granularity_bytes : int or Param[int]
        Write chunk size in bytes. Defaults to ``65536``. Larger writes have
        less overhead but may pollute CPU caches.

    Returns
    -------
    Subgraph
        A subgraph with input ``"input"`` and no outputs that writes events of
        ``event_type`` to the file.

    See Also
    --------
    :py:obj:`WriteBinaryStream`
    :py:obj:`Batch`
    :py:obj:`ViewAsBytes`
    """
    g = Graph()
    g.add_chain(
        [
            ("batcher", Batch(event_type, batch_size=batch_size)),
            ViewAsBytes(event_type),
            WriteBinaryStream(
                _streams.BinaryFileOutputStream(
                    filename, truncate=truncate, append=append
                ),
                write_granularity_bytes=write_granularity_bytes,
            ),
        ]
    )
    return Subgraph(g, input_map={"input": ("batcher", "input")})
