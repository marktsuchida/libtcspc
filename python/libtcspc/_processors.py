# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Iterable, Sequence
from typing import final

from typing_extensions import override

from . import _access, _cpp_utils, _events, _streams
from ._access import AccessTag, _AccessSpec
from ._acquisition_readers import AcquisitionReader, PyAcquisitionReader
from ._bucket_sources import BucketSource, RecyclingBucketSource
from ._codegen import _CodeGenerationContext
from ._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _size_type,
    _string_type,
    _uint64_type,
)
from ._data_types import DataTypes
from ._events import BucketEvent, EventType, WarningEvent
from ._graph import Graph, Subgraph
from ._node import Node, _RelayNode, _TypePreservingRelayNode
from ._param import Param


def _check_events_subset_of(
    input_events: Iterable[EventType],
    allowed_events: Iterable[EventType],
    processor: str,
) -> None:
    for t in input_events:
        if not _cpp_utils._contains_type(
            (u._cpp_type_name() for u in allowed_events), t._cpp_type_name()
        ):
            raise ValueError(f"input type {t} not accepted by {processor}")


def _remove_events_from_set(
    input_events: Iterable[EventType], events_to_remove: Iterable[EventType]
) -> tuple[EventType, ...]:
    return tuple(
        t
        for t in input_events
        if not _cpp_utils._contains_type(
            (u._cpp_type_name() for u in events_to_remove), t._cpp_type_name()
        )
    )


def _make_type_list(event_types: Iterable[EventType]) -> _CppTypeName:
    return _CppTypeName(
        "tcspc::type_list<{}>".format(
            ", ".join(t._cpp_type_name() for t in event_types)
        )
    )


def _bucket_source_or_default(
    event_type: EventType, arg: BucketSource | None
) -> BucketSource:
    return arg if arg is not None else RecyclingBucketSource(event_type)


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
    ReadBinaryStream
    Unbatch
    Stop
    StopWithError
    """
    g = Graph()
    g.add_sequence(
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


# Note: Wrappers of C++ processors are ordered alphabetically without regard to
# the C++ header in which they are defined.


@final
class Acquire(_RelayNode):
    """Source processor that acquires data into buckets via a pull-style reader.

    Used to plug a hardware acquisition driver — or any pull-based data
    source — into a libtcspc processing graph. On each iteration the
    processor obtains an empty bucket from the buffer provider, calls the
    reader to fill it, and emits the (possibly partially filled) bucket as
    a `BucketEvent` downstream.

    The acquisition runs until the reader signals end of stream (by
    returning ``None``), the reader raises an exception, or the
    acquisition is halted via the `AcquireAccess` retrieved from the
    `ExecutionContext` using ``access_tag``. Halting is asynchronous: the
    ``halt()`` call returns immediately, but the acquisition may continue
    briefly. Wait for graph execution to finish before tearing down
    resources used by the reader.

    Parameters
    ----------
    event_type : EventType
        Element type of the acquired data (typically a byte or integer
        type). Each emitted bucket holds a contiguous array of this type.
    reader : AcquisitionReader or Param[PyAcquisitionReader]
        Object that fills supplied buffers with acquired data on each
        call. Pass an `AcquisitionReader` instance (such as `NullReader`)
        to use a built-in C++-side reader, or wrap a Python callable in
        a runtime `Param` of type `PyAcquisitionReader` to bind it at
        execution time.
    buffer_provider : BucketSource or None
        Source of buckets used to hold each batch. If ``None``, a default
        `RecyclingBucketSource` for ``event_type`` is used.
    batch_size : int or Param[int] or None
        Number of elements requested per read. Smaller values reduce
        latency; larger values reduce per-read overhead. Defaults to
        ``65536``. Must be positive.
    access_tag : AccessTag
        Tag used to retrieve an `AcquireAccess` (which provides ``halt()``)
        from the `ExecutionContext` at runtime.

    Notes
    -----
    Events handled:

    - This processor has no input events; it is a source.
    - Emits `BucketEvent` of ``event_type`` whenever a non-empty read
      completes.
    - End of input is initiated when the reader returns ``None``,
      whereupon the downstream is flushed. If halted via `AcquireAccess`
      before end of stream, the downstream is **not** flushed and a halt
      exception is raised.

    See Also
    --------
    :cpp:`tcspc::acquire`
        The underlying C++ factory function.
    AcquisitionReader
        Interface for built-in C++-side readers.
    PyAcquisitionReader
        Interface for Python-callable readers (used via `Param`).
    AcquireAccess
        Runtime access object providing ``halt()``.
    """

    def __init__(
        self,
        event_type: EventType,
        reader: AcquisitionReader | Param[PyAcquisitionReader],
        buffer_provider: BucketSource | None,
        batch_size: int | Param[int] | None,
        access_tag: _access.AccessTag,
    ) -> None:
        self._event_type = event_type
        self._reader = reader
        self._bucket_source = _bucket_source_or_default(
            event_type, buffer_provider
        )
        self._batch_size = batch_size if batch_size is not None else 65536
        self._access_tag = access_tag

    @override
    def _accesses(self) -> Sequence[tuple[AccessTag, type[_AccessSpec]]]:
        return ((self._access_tag, _access._AcquireAccessSpec),)

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(input_event_set, (), self.__class__.__name__)
        return (_events.BucketEvent(self._event_type),)

    def _buffer_array_type(self) -> _CppTypeName:
        return _CppTypeName(f"""\
            nanobind::ndarray<
                {self._event_type._cpp_type_name()},
                nanobind::numpy, nanobind::device::cpu, nanobind::c_contig>""")

    def _buffer_array_param_type(self) -> _CppTypeName:
        return _CppTypeName(f"""\
            decltype(std::declval<{self._buffer_array_type()}>()
                .cast(nanobind::rv_policy::reference))""")

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._reader, Param):
            params.append(
                (
                    self._reader,
                    _CppTypeName(
                        f"""\
                        std::function<
                            auto({self._buffer_array_param_type()})
                            -> std::optional<std::size_t>>"""
                    ),
                )
            )
        if isinstance(self._batch_size, Param):
            params.append((self._batch_size, _size_type))
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        reader = (
            _CppExpression(
                f"""\
                [reader={gencontext.params_varname}.{self._reader._cpp_identifier()}](
                    std::span<{self._event_type._cpp_type_name()}> spn) {{
                    nanobind::gil_scoped_acquire held;
                    {self._buffer_array_type()} arr(spn.data(), {{spn.size()}});
                    return reader(arr.cast(nanobind::rv_policy::reference));
                }}
                """
            )
            if isinstance(self._reader, Param)
            else self._reader._cpp_expression()
        )
        batch_size = gencontext.size_t_expression(self._batch_size)
        return _CppExpression(
            f"""\
            tcspc::acquire<{self._event_type._cpp_type_name()}>(
                {reader},
                {self._bucket_source._cpp_expression(gencontext)},
                tcspc::arg::batch_size{{{batch_size}}},
                {gencontext.tracker_expression(_CppTypeName("tcspc::acquire_access"), self._access_tag)},
                {downstream}
            )"""
        )


@final
class Batch(_RelayNode):
    """Processor that groups events into fixed-size buckets.

    Collects every ``batch_size`` events of ``event_type`` into a bucket
    obtained from the buffer provider, and emits the bucket as a
    `BucketEvent` once full. Batching does not perform time-based flushing,
    so it can introduce arbitrary latency on real-time event streams; for
    that reason, intermediate buffering of real-time streams is usually
    unnecessary and inadvisable.

    Parameters
    ----------
    event_type : EventType
        The event type to batch. The input event set must consist only of
        this type.
    buffer_provider : BucketSource or None
        Source of buckets used to hold each batch. If ``None``, a default
        `RecyclingBucketSource` for ``event_type`` is used.
    batch_size : int or Param[int] or None
        Number of events to collect in each bucket. Defaults to ``65536``.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: collected into `BucketEvent`;
      emitted when the bucket is full.
    - All other event types: rejected at graph build time.
    - End of input: emit any partially-filled bucket, then pass through.

    See Also
    --------
    :cpp:`tcspc::batch`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_type: EventType,
        *,
        buffer_provider: BucketSource | None = None,
        batch_size: int | Param[int] | None = None,
    ) -> None:
        self._event_type = event_type
        self._bucket_source = _bucket_source_or_default(
            event_type, buffer_provider
        )
        self._batch_size = batch_size if batch_size is not None else 65536

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._batch_size, Param):
            params.append((self._batch_size, _size_type))
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (self._event_type,), self.__class__.__name__
        )
        return (_events.BucketEvent(self._event_type),)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        batch_size = gencontext.size_t_expression(self._batch_size)
        return _CppExpression(
            f"""\
            tcspc::batch<{self._event_type._cpp_type_name()}>(
                {self._bucket_source._cpp_expression(gencontext)},
                tcspc::arg::batch_size{{{batch_size}}},
                {downstream}
            )"""
        )


@final
class CheckMonotonic(_TypePreservingRelayNode):
    """Pass-through processor that checks event timestamps are non-decreasing.

    For each event that carries an ``abstime`` field, this processor checks
    that the timestamp is greater than or equal to the previous one. On
    violation, a `WarningEvent` is emitted immediately before the
    offending event, which is itself then passed through. Useful for
    catching gross data-format problems, such as misinterpreting the
    record layout or reading binary data in text mode.

    Parameters
    ----------
    data_types : DataTypes or None
        Data type set specifying the ``abstime`` type expected on input
        events. Defaults to ``DataTypes()``.

    Notes
    -----
    Events handled:

    - Events with an ``abstime`` field: check that ``abstime`` is
      non-decreasing; if violated, emit a `WarningEvent` just before the
      offending event, then pass through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::check_monotonic`
        The underlying C++ factory function.
    """

    def __init__(self, data_types: DataTypes | None = None) -> None:
        self._data_types = (
            data_types if data_types is not None else DataTypes()
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::check_monotonic<{self._data_types._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class Count(_TypePreservingRelayNode):
    """Processor that counts events of a given type, passing every event through.

    The running count can be retrieved at any time during execution via
    the `CountAccess` retrieved from the `ExecutionContext` using
    ``access_tag``. The counter is incremented before each matching
    event is forwarded, so an observer reading the count immediately
    after the Nth matching event sees the value N.

    Parameters
    ----------
    event_type : EventType
        The event type whose occurrences are counted. Other event types
        are forwarded but not counted.
    access_tag : AccessTag
        Tag used to retrieve a `CountAccess` from the `ExecutionContext`
        at runtime.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: increment counter, then pass through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::count`
        The underlying C++ factory function.
    """

    def __init__(self, event_type: EventType, access_tag: AccessTag) -> None:
        self._event_type = event_type
        self._access_tag = access_tag

    @override
    def _accesses(
        self,
    ) -> Sequence[tuple[AccessTag, type[_AccessSpec]]]:
        return ((self._access_tag, _access._CountAccessSpec),)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::count<{self._event_type._cpp_type_name()}>(
                {gencontext.tracker_expression(_CppTypeName("tcspc::count_access"), self._access_tag)},
                {downstream}
            )"""
        )


@final
class DecodeBHSPC(_RelayNode):
    """Processor that decodes Becker & Hickl SPC FIFO records into libtcspc TCSPC events.

    Decoder for SPC-130, 830, 140, 930, 150, 130EM, 150N (NX, NXX),
    130EMN, 160 (X, PCIE), 180N (NX, NXX), and 130IN (INX, INXX). For
    SPC-160 and SPC-180N, the fast intensity counter is not decoded; the
    processor can still be used for these models if the counter value is
    not of interest.

    Parameters
    ----------
    data_types : DataTypes or None
        Data type set specifying the ``abstime``, ``channel``, and
        ``difftime`` types of the emitted events. Defaults to
        ``DataTypes()``.

    Notes
    -----
    Events handled:

    - `BHSPCEvent`: decoded; for each input record, zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `MarkerEvent`,
      and `DataLostEvent` are emitted. A `WarningEvent` is emitted when
      an invalid record is encountered.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_bh_spc`
        The underlying C++ factory function.
    """

    def __init__(self, data_types: DataTypes | None = None) -> None:
        self._data_types = (
            data_types if data_types is not None else DataTypes()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (_events.BHSPCEvent(),), self.__class__.__name__
        )
        return (
            _events.DataLostEvent(self._data_types),
            _events.MarkerEvent(self._data_types),
            _events.TimeCorrelatedDetectionEvent(self._data_types),
            _events.TimeReachedEvent(self._data_types),
            WarningEvent(),
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_bh_spc<{self._data_types._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class NullSink(Node):
    """Sink that discards every event it receives.

    Notes
    -----
    Events handled:

    - All event types: ignored.
    - End of input: ignored.

    See Also
    --------
    :cpp:`tcspc::null_sink`
        The underlying C++ factory function.
    """

    def __init__(self) -> None:
        super().__init__(output=())

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        return ()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        return _CppExpression("tcspc::null_sink()")


@final
class NullSource(_RelayNode):
    """Source processor that emits no events.

    Notes
    -----
    Events handled:

    - This processor has no input events; it is a source.
    - Emits no events.
    - End of input: pass through (flushes the downstream once).

    See Also
    --------
    :cpp:`tcspc::null_source`
        The underlying C++ factory function.
    """

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(input_event_set, (), self.__class__.__name__)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(f"tcspc::null_source({downstream})")


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
    buffer_provider : BucketSource or None
        Source of buckets used to hold each read. If ``None``, a default
        `RecyclingBucketSource` for ``event_type`` is used. Must be able
        to circulate at least 2 buckets without blocking.
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
    """

    def __init__(
        self,
        event_type: EventType,
        stream: _streams.InputStream,
        max_length: int | Param[int] | None = None,
        buffer_provider: BucketSource | None = None,
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
class SelectAll(_TypePreservingRelayNode):
    """Pass-through processor that forwards every event unchanged.

    This is a no-op filter. It is occasionally useful as a placeholder
    where the graph structure expects a processor but no transformation
    is needed.

    Notes
    -----
    Events handled:

    - All event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::select_all`
        The underlying C++ factory function.
    """

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(f"tcspc::select_all({downstream})")


@final
class SinkEvents(Node):
    """Sink that accepts and discards a fixed set of event types.

    Most useful for testing, or for asserting at graph-build time that an
    upstream subgraph only produces a known set of event types — any
    other event type connected to this sink is rejected at graph build
    time.

    Parameters
    ----------
    *event_types : EventType
        The only event types the sink will accept.

    Notes
    -----
    Events handled:

    - Events matching one of ``event_types``: ignored.
    - Events not in ``event_types``: rejected at graph build time.
    - End of input: ignored.

    See Also
    --------
    :cpp:`tcspc::sink_events`
        The underlying C++ factory function.
    """

    def __init__(self, *event_types: EventType) -> None:
        super().__init__(output=())
        self._event_types = tuple(event_types)

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 1:
            raise ValueError(
                f"wrong number of inputs (1 expected, {len(input_event_sets)} found)"
            )
        _check_events_subset_of(
            input_event_sets[0], self._event_types, self.__class__.__name__
        )
        return ()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        evts = ", ".join(t._cpp_type_name() for t in self._event_types)
        return _CppExpression(f"tcspc::sink_events<{evts}>()")


@final
class Stop(_RelayNode):
    """Pass-through processor that ends the stream normally on a configured event.

    When an event matching one of ``event_types`` is received, the
    downstream is flushed and processing ends normally (the Python
    binding surfaces this as `EndOfProcessing`, with the triggering
    event included in the message). All other events pass through
    unchanged.

    Parameters
    ----------
    event_types : Iterable[EventType]
        Event types that trigger normal termination.
    message_prefix : str or Param[str]
        String prepended to the `EndOfProcessing` exception's message,
        typically describing the source of the termination.

    Notes
    -----
    Events handled:

    - Events matching one of ``event_types``: flush the downstream, then
      end processing normally.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::stop`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_types: Iterable[EventType],
        message_prefix: str | Param[str],
    ) -> None:
        self._event_types = list(event_types)
        self._msg_prefix = message_prefix

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._msg_prefix, Param):
            return ((self._msg_prefix, _string_type),)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::stop<
                {_make_type_list(self._event_types)}
            >(
                {gencontext.string_expression(self._msg_prefix)},
                {downstream}
            )"""
        )


@final
class StopWithError(_RelayNode):
    """Pass-through processor that ends the stream with an error on a configured event.

    When an event matching one of ``event_types`` is received, processing
    ends with an error: a `RuntimeError`-equivalent is raised on the
    Python side, with ``message_prefix`` included in the message. Unlike
    `Stop`, the downstream is **not** flushed. All other events pass
    through unchanged.

    Parameters
    ----------
    event_types : Iterable[EventType]
        Event types that trigger error termination.
    message_prefix : str or Param[str]
        String prepended to the raised exception's message, typically
        describing the source of the error condition.

    Notes
    -----
    Events handled:

    - Events matching one of ``event_types``: end processing with an
      error. The downstream is **not** flushed.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::stop_with_error`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_types: Iterable[EventType],
        message_prefix: str | Param[str],
    ) -> None:
        self._event_types = list(event_types)
        self._msg_prefix = message_prefix

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._msg_prefix, Param):
            return ((self._msg_prefix, _string_type),)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::stop_with_error<
                {_make_type_list(self._event_types)}
            >(
                {gencontext.string_expression(self._msg_prefix)},
                {downstream}
            )"""
        )


@final
class Unbatch(_RelayNode):
    """Processor that emits the elements of bucketed batches one at a time.

    The inverse of `Batch`: for each incoming `BucketEvent`, each
    contained element is emitted in order as an individual event of the
    bucket's element type.

    Parameters
    ----------
    event_type : BucketEvent
        The bucket event type whose elements are to be unpacked. The
        emitted event type is the bucket's element type.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: each contained element is emitted
      in order as an individual event.
    - Events not matching ``event_type``: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::unbatch`
        The underlying C++ factory function.
    """

    def __init__(self, event_type: BucketEvent) -> None:
        self._event_type = event_type

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        for ie in input_event_set:
            if ie != self._event_type:
                raise ValueError("incorrect input event type")
        return (self._event_type.element_event_type(),)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::unbatch<{self._event_type._cpp_type_name()}>(
                {downstream}
            )"""
        )
