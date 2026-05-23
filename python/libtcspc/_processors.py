# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Collection, Iterable, Mapping, Sequence
from typing import Any, final

from typing_extensions import override

from . import _access, _cpp_utils, _events, _streams
from ._access import AccessTag, _AccessSpec
from ._acquisition_readers import AcquisitionReader, PyAcquisitionReader
from ._bucket_sources import (
    BucketSource,
    PyBucketSource,
    RecyclingBucketSource,
    _PyBucketSource,
)
from ._codegen import _CodeGenerationContext
from ._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _int64_type,
    _size_type,
    _string_type,
    _uint64_type,
)
from ._events import BucketEvent, EventType, WarningEvent
from ._graph import Graph, Subgraph
from ._node import Node, _RelayNode, _TypePreservingRelayNode
from ._numeric_traits import NumericTraits
from ._param import Param
from ._routers import Router


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
    event_type: EventType,
    arg: BucketSource | Param[PyBucketSource] | None,
) -> BucketSource:
    if isinstance(arg, Param):
        return _PyBucketSource(event_type, arg)
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


def _acquisition_buffer_array_type(event_type: EventType) -> _CppTypeName:
    return _CppTypeName(f"""\
            nanobind::ndarray<
                {event_type._cpp_type_name()},
                nanobind::numpy, nanobind::device::cpu, nanobind::c_contig>""")


def _acquisition_buffer_array_param_type(
    event_type: EventType,
) -> _CppTypeName:
    return _CppTypeName(f"""\
            decltype(std::declval<{_acquisition_buffer_array_type(event_type)}>()
                .cast(nanobind::rv_policy::reference))""")


def _acquisition_reader_param_type(event_type: EventType) -> _CppTypeName:
    return _CppTypeName(
        f"""\
                        std::function<
                            auto({_acquisition_buffer_array_param_type(event_type)})
                            -> std::optional<std::size_t>>"""
    )


def _acquisition_reader_cpp_expression(
    gencontext: _CodeGenerationContext,
    reader: AcquisitionReader | Param[PyAcquisitionReader],
    event_type: EventType,
) -> _CppExpression:
    if isinstance(reader, Param):
        return _CppExpression(
            f"""\
                [reader={gencontext.params_varname}.{reader._cpp_identifier()}](
                    std::span<{event_type._cpp_type_name()}> spn) {{
                    nanobind::gil_scoped_acquire held;
                    {_acquisition_buffer_array_type(event_type)} arr(spn.data(), {{spn.size()}});
                    return reader(arr.cast(nanobind::rv_policy::reference));
                }}
                """
        )
    return reader._cpp_expression()


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
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets used to hold each batch. If ``None``, a default
        `RecyclingBucketSource` for ``event_type`` is used. A runtime
        `Param` of type `PyBucketSource` binds a Python bucket source at
        execution time.
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
    :py:obj:`AcquisitionReader`
        Interface for built-in C++-side readers.
    :py:obj:`PyAcquisitionReader`
        Interface for Python-callable readers (used via `Param`).
    :py:obj:`AcquireAccess`
        Runtime access object providing ``halt()``.
    """

    def __init__(
        self,
        event_type: EventType,
        reader: AcquisitionReader | Param[PyAcquisitionReader],
        buffer_provider: BucketSource | Param[PyBucketSource] | None,
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

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._reader, Param):
            params.append(
                (
                    self._reader,
                    _acquisition_reader_param_type(self._event_type),
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
        reader = _acquisition_reader_cpp_expression(
            gencontext, self._reader, self._event_type
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
class AcquireFullBuckets(Node):
    """Source processor that acquires data into fixed-size buckets while also delivering real-time read-only views.

    Like `Acquire`, this plugs a pull-based data source into a libtcspc
    processing graph: on each iteration the processor obtains an empty bucket
    from the buffer provider, calls the reader to fill it, and collects the
    data into fixed-size buckets. Unlike `Acquire`, it has two outputs:

    - ``live``: a read-only `ConstBucketEvent` view of the data read on each
      individual read, delivered immediately (typically used for live display).
    - ``batch``: a full `BucketEvent` bucket emitted whenever ``batch_size``
      elements have been collected, plus any partial final bucket on flush
      (typically used for saving to disk).

    The ``live`` views are zero-copy when the buffer provider supplies
    shared-view-capable buckets, so they must not be modified.

    The acquisition runs until the reader signals end of stream (by returning
    ``None``), the reader raises an exception, or the acquisition is halted via
    the `AcquireAccess` retrieved from the `ExecutionContext` using
    ``access_tag``. Halting is asynchronous: the ``halt()`` call returns
    immediately, but the acquisition may continue briefly. Wait for graph
    execution to finish before tearing down resources used by the reader.

    Parameters
    ----------
    event_type : EventType
        Element type of the acquired data (typically a byte or integer type).
        Each emitted bucket holds a contiguous array of this type.
    reader : AcquisitionReader or Param[PyAcquisitionReader]
        Object that fills supplied buffers with acquired data on each call.
        Pass an `AcquisitionReader` instance (such as `NullReader`) to use a
        built-in C++-side reader, or wrap a Python callable in a runtime `Param`
        of type `PyAcquisitionReader` to bind it at execution time.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets used to hold each batch. If ``None``, a default
        `RecyclingBucketSource` for ``event_type`` is used. A runtime `Param` of
        type `PyBucketSource` binds a Python bucket source at execution time.
        The buffer provider must support shared views unless the ``live`` output
        is connected to a `SinkAll`.
    batch_size : int or Param[int] or None
        Number of elements collected in each full bucket. Smaller values reduce
        latency; larger values reduce per-read overhead. Defaults to ``65536``.
        Must be positive.
    access_tag : AccessTag
        Tag used to retrieve an `AcquireAccess` (which provides ``halt()``) from
        the `ExecutionContext` at runtime.

    Notes
    -----
    Events handled:

    - This processor has no input events; it is a source.
    - Emits `ConstBucketEvent` of ``event_type`` on the ``live`` output for each
      non-empty read, and `BucketEvent` of ``event_type`` on the ``batch``
      output whenever a full bucket is collected.
    - End of input is initiated when the reader returns ``None``, whereupon both
      downstreams are flushed (any partial bucket is emitted on ``batch``
      first). If halted via `AcquireAccess` before end of stream, the
      downstreams are **not** flushed and a halt exception is raised.

    See Also
    --------
    :cpp:`tcspc::acquire_full_buckets`
        The underlying C++ factory function.
    :py:obj:`Acquire`
        The single-output counterpart.
    :py:obj:`AcquisitionReader`
        Interface for built-in C++-side readers.
    :py:obj:`PyAcquisitionReader`
        Interface for Python-callable readers (used via `Param`).
    :py:obj:`AcquireAccess`
        Runtime access object providing ``halt()``.
    """

    def __init__(
        self,
        event_type: EventType,
        reader: AcquisitionReader | Param[PyAcquisitionReader],
        buffer_provider: BucketSource | Param[PyBucketSource] | None,
        batch_size: int | Param[int] | None,
        access_tag: _access.AccessTag,
    ) -> None:
        super().__init__(output=("live", "batch"))
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
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 1:
            raise ValueError(
                f"wrong number of inputs (1 expected, {len(input_event_sets)} found)"
            )
        _check_events_subset_of(
            input_event_sets[0], (), self.__class__.__name__
        )
        return (
            (_events.ConstBucketEvent(self._event_type),),
            (_events.BucketEvent(self._event_type),),
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._reader, Param):
            params.append(
                (
                    self._reader,
                    _acquisition_reader_param_type(self._event_type),
                )
            )
        if isinstance(self._batch_size, Param):
            params.append((self._batch_size, _size_type))
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        if len(downstreams) != 2:
            raise ValueError(
                f"expected 2 downstreams; found {len(downstreams)}"
            )
        live, batch = downstreams
        reader = _acquisition_reader_cpp_expression(
            gencontext, self._reader, self._event_type
        )
        batch_size = gencontext.size_t_expression(self._batch_size)
        return _CppExpression(
            f"""\
            tcspc::acquire_full_buckets<{self._event_type._cpp_type_name()}>(
                {reader},
                {self._bucket_source._cpp_expression(gencontext)},
                tcspc::arg::batch_size{{{batch_size}}},
                {gencontext.tracker_expression(_CppTypeName("tcspc::acquire_access"), self._access_tag)},
                {live},
                {batch}
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
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets used to hold each batch. If ``None``, a default
        `RecyclingBucketSource` for ``event_type`` is used. A runtime
        `Param` of type `PyBucketSource` binds a Python bucket source at
        execution time.
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
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
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
class CheckAlternating(_RelayNode):
    """Pass-through processor that checks two event types alternate.

    Verifies that events of types ``event0_type`` and ``event1_type``
    appear in strict alternation starting with ``event0_type``. On
    violation, a `WarningEvent` is emitted just before the offending
    event. All events (including warnings) are then passed through.

    Parameters
    ----------
    event0_type : EventType
        The event type expected first in each alternation.
    event1_type : EventType
        The event type expected to follow ``event0_type``.

    Notes
    -----
    Events handled:

    - ``event0_type``, ``event1_type``: if not strictly alternating
      starting with ``event0_type``, emit `WarningEvent` just before
      passing through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    The output event set always includes `WarningEvent`, even if the
    upstream event set does not.

    See Also
    --------
    :cpp:`tcspc::check_alternating`
        The underlying C++ factory function.
    """

    def __init__(self, event0_type: EventType, event1_type: EventType) -> None:
        self._event0_type = event0_type
        self._event1_type = event1_type

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        if _cpp_utils._contains_type(
            (t._cpp_type_name() for t in input_event_set),
            WarningEvent()._cpp_type_name(),
        ):
            return tuple(input_event_set)
        return (*input_event_set, WarningEvent())

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::check_alternating<
                {self._event0_type._cpp_type_name()},
                {self._event1_type._cpp_type_name()}
            >(
                {downstream}
            )"""
        )


@final
class CheckMonotonic(_RelayNode):
    """Pass-through processor that checks event timestamps are non-decreasing.

    For each event that carries an ``abstime`` field, this processor checks
    that the timestamp is greater than or equal to the previous one. On
    violation, a `WarningEvent` is emitted immediately before the
    offending event, which is itself then passed through. Useful for
    catching gross data-format problems, such as misinterpreting the
    record layout or reading binary data in text mode.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime`` type expected on input
        events. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - Events with an ``abstime`` field: check that ``abstime`` is
      non-decreasing; if violated, emit a `WarningEvent` just before the
      offending event, then pass through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    The output event set always includes `WarningEvent`, even if the
    upstream event set does not.

    See Also
    --------
    :cpp:`tcspc::check_monotonic`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        if _cpp_utils._contains_type(
            (t._cpp_type_name() for t in input_event_set),
            WarningEvent()._cpp_type_name(),
        ):
            return tuple(input_event_set)
        return (*input_event_set, WarningEvent())

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::check_monotonic<{self._numeric_traits._cpp_type_name()}>(
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
class CountDownTo(_RelayNode):
    """Processor that counts down on a tick event and fires when a threshold is reached.

    The mirror of `CountUpTo`. The internal counter starts at
    ``initial_count`` and is decremented each time a `TickEvent` is
    received. ``limit`` must be less than ``initial_count``. See
    `CountUpTo` for full semantics.

    Parameters
    ----------
    tick_event_type : EventType
        Event type that increments (here, decrements) the counter.
    fire_event_type : EventType
        Event type to emit when the count reaches ``threshold``. Must
        be brace-initializable with an ``abstime``.
    reset_event_type : EventType
        Event type that resets the counter to ``initial_count``.
    threshold : int or Param[int]
        Count value at which `FireEvent` is emitted.
    limit : int or Param[int]
        Count value at which the counter is reset to ``initial_count``.
        Must be less than ``initial_count``.
    initial_count : int or Param[int]
        Starting and reset value of the counter.
    fire_after_tick : bool
        If ``True``, the fire event is emitted after the tick event is
        passed through (after the count is decremented); otherwise it is
        emitted before. Defaults to ``False``.

    Notes
    -----
    Events handled:

    - ``tick_event_type``: pass through and decrement; emit
      ``fire_event_type`` (before or after, per ``fire_after_tick``) on
      threshold; reset on limit.
    - ``reset_event_type``: reset counter; pass through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::count_down_to`
        The underlying C++ factory function.
    :py:obj:`CountUpTo`
        The counterpart that counts up to a threshold.
    """

    def __init__(
        self,
        tick_event_type: EventType,
        fire_event_type: EventType,
        reset_event_type: EventType,
        threshold: int | Param[int],
        limit: int | Param[int],
        initial_count: int | Param[int],
        *,
        fire_after_tick: bool = False,
    ) -> None:
        self._tick_event_type = tick_event_type
        self._fire_event_type = fire_event_type
        self._reset_event_type = reset_event_type
        self._threshold = threshold
        self._limit = limit
        self._initial_count = initial_count
        self._fire_after_tick = fire_after_tick

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._threshold, Param):
            params.append((self._threshold, _uint64_type))
        if isinstance(self._limit, Param):
            params.append((self._limit, _uint64_type))
        if isinstance(self._initial_count, Param):
            params.append((self._initial_count, _uint64_type))
        return params

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        out = list(input_event_set)
        if not _cpp_utils._contains_type(
            (t._cpp_type_name() for t in out),
            self._fire_event_type._cpp_type_name(),
        ):
            out.append(self._fire_event_type)
        return tuple(out)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        threshold = gencontext.u64_expression(self._threshold)
        limit = gencontext.u64_expression(self._limit)
        initial = gencontext.u64_expression(self._initial_count)
        fat = "true" if self._fire_after_tick else "false"
        return _CppExpression(
            f"""\
            tcspc::count_down_to<
                {self._tick_event_type._cpp_type_name()},
                {self._fire_event_type._cpp_type_name()},
                {self._reset_event_type._cpp_type_name()},
                {fat}
            >(
                tcspc::arg::threshold<tcspc::u64>{{{threshold}}},
                tcspc::arg::limit<tcspc::u64>{{{limit}}},
                tcspc::arg::initial_count<tcspc::u64>{{{initial}}},
                {downstream}
            )"""
        )


@final
class CountUpTo(_RelayNode):
    """Processor that counts up on a tick event and fires when a threshold is reached.

    The internal counter starts at ``initial_count`` and is incremented
    each time a `TickEvent` is received and passed through. When the
    counter equals ``threshold``, ``fire_event_type`` is emitted (just
    before or just after the tick, controlled by ``fire_after_tick``)
    with its ``abstime`` set to that of the triggering tick. When the
    counter equals ``limit``, it is reset to ``initial_count``. A
    `ResetEvent` resets the counter explicitly.

    Parameters
    ----------
    tick_event_type : EventType
        Event type that increments the counter. Must have an ``abstime``
        field.
    fire_event_type : EventType
        Event type to emit on threshold. Must be brace-initializable
        with an ``abstime``.
    reset_event_type : EventType
        Event type that resets the counter to ``initial_count``.
    threshold : int or Param[int]
        Count value at which `FireEvent` is emitted.
    limit : int or Param[int]
        Count value at which the counter is reset to ``initial_count``.
        Must be greater than ``initial_count``.
    initial_count : int or Param[int]
        Starting and reset value of the counter.
    fire_after_tick : bool
        If ``True``, the fire event is emitted after the tick event is
        passed through (after the count is incremented); otherwise it
        is emitted before. Defaults to ``False``.

    Notes
    -----
    Events handled:

    - ``tick_event_type``: pass through and increment; emit
      ``fire_event_type`` (before or after, per ``fire_after_tick``) on
      threshold; reset on limit.
    - ``reset_event_type``: reset counter; pass through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::count_up_to`
        The underlying C++ factory function.
    :py:obj:`CountDownTo`
        The counterpart that counts down to a threshold.
    """

    def __init__(
        self,
        tick_event_type: EventType,
        fire_event_type: EventType,
        reset_event_type: EventType,
        threshold: int | Param[int],
        limit: int | Param[int],
        initial_count: int | Param[int],
        *,
        fire_after_tick: bool = False,
    ) -> None:
        self._tick_event_type = tick_event_type
        self._fire_event_type = fire_event_type
        self._reset_event_type = reset_event_type
        self._threshold = threshold
        self._limit = limit
        self._initial_count = initial_count
        self._fire_after_tick = fire_after_tick

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._threshold, Param):
            params.append((self._threshold, _uint64_type))
        if isinstance(self._limit, Param):
            params.append((self._limit, _uint64_type))
        if isinstance(self._initial_count, Param):
            params.append((self._initial_count, _uint64_type))
        return params

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        out = list(input_event_set)
        if not _cpp_utils._contains_type(
            (t._cpp_type_name() for t in out),
            self._fire_event_type._cpp_type_name(),
        ):
            out.append(self._fire_event_type)
        return tuple(out)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        threshold = gencontext.u64_expression(self._threshold)
        limit = gencontext.u64_expression(self._limit)
        initial = gencontext.u64_expression(self._initial_count)
        fat = "true" if self._fire_after_tick else "false"
        return _CppExpression(
            f"""\
            tcspc::count_up_to<
                {self._tick_event_type._cpp_type_name()},
                {self._fire_event_type._cpp_type_name()},
                {self._reset_event_type._cpp_type_name()},
                {fat}
            >(
                tcspc::arg::threshold<tcspc::u64>{{{threshold}}},
                tcspc::arg::limit<tcspc::u64>{{{limit}}},
                tcspc::arg::initial_count<tcspc::u64>{{{initial}}},
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
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime``, ``channel``, and
        ``difftime`` types of the emitted events. Defaults to
        ``NumericTraits()``.

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

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (_events.BHSPCEvent(),), self.__class__.__name__
        )
        return (
            _events.DataLostEvent(self._numeric_traits),
            _events.MarkerEvent(self._numeric_traits),
            _events.TimeCorrelatedDetectionEvent(self._numeric_traits),
            _events.TimeReachedEvent(self._numeric_traits),
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
            tcspc::decode_bh_spc<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodeBHSPC600_256ch(_RelayNode):
    """Processor that decodes BH SPC-600/630 32-bit FIFO records (256-channel mode).

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime``, ``channel``, and
        ``difftime`` types of the emitted events. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BHSPC600_256chEvent`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `DataLostEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_bh_spc600_256ch`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.BHSPC600_256chEvent(),),
            self.__class__.__name__,
        )
        return (
            _events.DataLostEvent(self._numeric_traits),
            _events.TimeCorrelatedDetectionEvent(self._numeric_traits),
            _events.TimeReachedEvent(self._numeric_traits),
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
            tcspc::decode_bh_spc600_256ch<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodeBHSPC600_4096ch(_RelayNode):
    """Processor that decodes BH SPC-600/630 48-bit FIFO records (4096-channel mode).

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime``, ``channel``, and
        ``difftime`` types of the emitted events. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BHSPC600_4096chEvent`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `DataLostEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_bh_spc600_4096ch`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.BHSPC600_4096chEvent(),),
            self.__class__.__name__,
        )
        return (
            _events.DataLostEvent(self._numeric_traits),
            _events.TimeCorrelatedDetectionEvent(self._numeric_traits),
            _events.TimeReachedEvent(self._numeric_traits),
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
            tcspc::decode_bh_spc600_4096ch<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodeBHSPCWithIntensityCounter(_RelayNode):
    """Processor that decodes BH SPC FIFO records including fast intensity counter.

    For SPC-160 and SPC-180N devices. Like `DecodeBHSPC`, but the
    marker-0 records are also decoded into `BulkCountsEvent`.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime``, ``channel``, ``difftime``,
        and ``count`` types of the emitted events. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BHSPCEvent`: decoded; emits zero or more of `TimeReachedEvent`,
      `TimeCorrelatedDetectionEvent`, `BulkCountsEvent`, `MarkerEvent`,
      `DataLostEvent`, and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_bh_spc_with_intensity_counter`
        The underlying C++ factory function.
    :py:obj:`DecodeBHSPC`
        Decode standard Becker & Hickl SPC FIFO records.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.BHSPCEvent(),),
            self.__class__.__name__,
        )
        return (
            _events.BulkCountsEvent(self._numeric_traits),
            _events.DataLostEvent(self._numeric_traits),
            _events.MarkerEvent(self._numeric_traits),
            _events.TimeCorrelatedDetectionEvent(self._numeric_traits),
            _events.TimeReachedEvent(self._numeric_traits),
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
            tcspc::decode_bh_spc_with_intensity_counter<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


def _pqt2_decode_output(
    numeric_traits: NumericTraits,
) -> tuple[EventType, ...]:
    return (
        _events.DetectionEvent(numeric_traits),
        _events.MarkerEvent(numeric_traits),
        _events.TimeReachedEvent(numeric_traits),
        WarningEvent(),
    )


def _pqt3_decode_output(
    numeric_traits: NumericTraits,
) -> tuple[EventType, ...]:
    return (
        _events.MarkerEvent(numeric_traits),
        _events.TimeCorrelatedDetectionEvent(numeric_traits),
        _events.TimeReachedEvent(numeric_traits),
        WarningEvent(),
    )


@final
class DecodePQT2Generic(_RelayNode):
    """Processor that decodes PicoQuant T2 (Generic) FIFO records.

    Used with HydraHarp V2, MultiHarp, TimeHarp 260, and PicoHarp 330.
    Sync edges are reported as `DetectionEvent` on channel ``-1``.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime`` and ``channel`` types.
        Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT2GenericEvent`: decoded; emits zero or more of
      `TimeReachedEvent`, `DetectionEvent`, `MarkerEvent`, and
      `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt2_generic`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT2GenericEvent(),),
            self.__class__.__name__,
        )
        return _pqt2_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt2_generic<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT2HydraHarpV1(_RelayNode):
    """Processor that decodes PicoQuant HydraHarp V1 T2 FIFO records.

    Sync edges are reported as `DetectionEvent` on channel ``-1``.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime`` and ``channel`` types.
        Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT2HydraHarpV1Event`: decoded; emits zero or more of
      `TimeReachedEvent`, `DetectionEvent`, `MarkerEvent`, and
      `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt2_hydraharpv1`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT2HydraHarpV1Event(),),
            self.__class__.__name__,
        )
        return _pqt2_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt2_hydraharpv1<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT2PicoHarp300(_RelayNode):
    """Processor that decodes PicoQuant PicoHarp 300 T2 FIFO records.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime`` and ``channel`` types.
        Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT2PicoHarp300Event`: decoded; emits zero or more of
      `TimeReachedEvent`, `DetectionEvent`, `MarkerEvent`, and
      `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt2_picoharp300`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT2PicoHarp300Event(),),
            self.__class__.__name__,
        )
        return _pqt2_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt2_picoharp300<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT3Generic(_RelayNode):
    """Processor that decodes PicoQuant T3 (Generic) FIFO records.

    Used with HydraHarp V2, MultiHarp, TimeHarp 260, and PicoHarp 330.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime``, ``channel``, and
        ``difftime`` types. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT3GenericEvent`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `MarkerEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt3_generic`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT3GenericEvent(),),
            self.__class__.__name__,
        )
        return _pqt3_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt3_generic<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT3HydraHarpV1(_RelayNode):
    """Processor that decodes PicoQuant HydraHarp V1 T3 FIFO records.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime``, ``channel``, and
        ``difftime`` types. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT3HydraHarpV1Event`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `MarkerEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt3_hydraharpv1`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT3HydraHarpV1Event(),),
            self.__class__.__name__,
        )
        return _pqt3_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt3_hydraharpv1<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodePQT3PicoHarp300(_RelayNode):
    """Processor that decodes PicoQuant PicoHarp 300 T3 FIFO records.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime``, ``channel``, and
        ``difftime`` types. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `PQT3PicoHarp300Event`: decoded; emits zero or more of
      `TimeReachedEvent`, `TimeCorrelatedDetectionEvent`, `MarkerEvent`,
      and `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_pqt3_picoharp300`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.PQT3PicoHarp300Event(),),
            self.__class__.__name__,
        )
        return _pqt3_decode_output(self._numeric_traits)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::decode_pqt3_picoharp300<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class DecodeSwabianTags(_RelayNode):
    """Processor that decodes 16-byte Swabian Time Tagger tags.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime``, ``channel``, and
        ``count`` types. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `SwabianTagEvent`: decoded; emits one of `DetectionEvent`,
      `BeginLostIntervalEvent`, `EndLostIntervalEvent`,
      `LostCountsEvent`, or `WarningEvent`.
    - All other event types: rejected at graph build time.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::decode_swabian_tags`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set,
            (_events.SwabianTagEvent(),),
            self.__class__.__name__,
        )
        return (
            _events.BeginLostIntervalEvent(self._numeric_traits),
            _events.DetectionEvent(self._numeric_traits),
            _events.EndLostIntervalEvent(self._numeric_traits),
            _events.LostCountsEvent(self._numeric_traits),
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
            tcspc::decode_swabian_tags<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class Delay(_TypePreservingRelayNode):
    """Pass-through processor that offsets event abstimes by a constant delta.

    Adds ``delta`` to the ``abstime`` field of every event that has one.
    Wrap-around is handled correctly even if ``abstime_type`` is a signed
    integer type. Only events with an ``abstime`` field may flow through.

    Parameters
    ----------
    delta : int or Param[int]
        Offset added to ``abstime``. May be negative.
    numeric_traits : NumericTraits or None
        Numeric traits specifying the ``abstime_type``. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - All event types with an ``abstime`` field: pass through with
      ``delta`` added.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::delay`
        The underlying C++ factory function.
    :py:obj:`RebaseAbstime`
        Shift abstime so the first event is at zero.
    """

    def __init__(
        self,
        delta: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._delta = delta
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._delta, Param):
            return ((self._delta, _int64_type),)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        abstime_t = f"{self._numeric_traits._cpp_type_name()}::abstime_type"
        if isinstance(self._delta, Param):
            value_expr = (
                f"{gencontext.params_varname}.{self._delta._cpp_identifier()}"
            )
        else:
            value_expr = f"tcspc::i64{{{self._delta}LL}}"
        return _CppExpression(
            f"""\
            tcspc::delay<{self._numeric_traits._cpp_type_name()}>(
                tcspc::arg::delta<{abstime_t}>{{static_cast<{abstime_t}>({value_expr})}},
                {downstream}
            )"""
        )


@final
class Broadcast(Node):
    """Broadcasts every event to several downstream processors.

    Each event received is forwarded to every connected downstream. Likewise,
    end-of-input is propagated to every downstream.

    Parameters
    ----------
    *event_types : EventType
        The event types to broadcast. Every output must be connected to a
        downstream that handles all of these types.
    outputs : int or Sequence[str]
        The output ports. An integer ``N`` creates ``N`` ports named
        ``"output-0"`` through ``"output-(N-1)"``. A sequence of names creates
        ports with those exact names. Must specify at least one output.

    Notes
    -----
    Events handled:

    - Events matching one of ``event_types``: broadcast to every downstream.
    - Events not in ``event_types``: rejected at graph build time.
    - End of input: broadcast to every downstream.

    See Also
    --------
    :cpp:`tcspc::broadcast`
        The underlying C++ factory function.
    """

    def __init__(
        self, *event_types: EventType, outputs: int | Sequence[str]
    ) -> None:
        if isinstance(outputs, int):
            if outputs < 1:
                raise ValueError("Broadcast requires at least one output")
            output_names: tuple[str, ...] = tuple(
                f"output-{i}" for i in range(outputs)
            )
        else:
            output_names = tuple(outputs)
            if len(output_names) < 1:
                raise ValueError("Broadcast requires at least one output")
            if len(set(output_names)) != len(output_names):
                raise ValueError("Broadcast output names must be unique")
        super().__init__(output=output_names)
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
        return tuple(self._event_types for _ in self.outputs())

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        n = len(self.outputs())
        if len(downstreams) != n:
            raise ValueError(
                f"expected {n} downstream(s); found {len(downstreams)}"
            )
        event_list = _make_type_list(self._event_types)
        args = ", ".join(downstreams)
        return _CppExpression(f"tcspc::broadcast<{event_list}>({args})")


@final
class Route(Node):
    """Routes events to one of several downstreams via a `Router`, broadcasting the rest.

    Each *routed* event is delivered to the single downstream selected by the
    router (or discarded if the router selects no output). Each *broadcast*
    event is delivered to every downstream. End-of-input is propagated to every
    downstream.

    Parameters
    ----------
    *routed_event_types : EventType
        The event types to route. The router selects which output each such
        event is sent to.
    broadcast_event_types : Sequence[EventType], keyword-only
        The event types to broadcast to every downstream. Must not overlap with
        the routed event types. Default: empty.
    router : Router, keyword-only
        The router that maps each routed event to an output-port index.
    outputs : int or Sequence[str], keyword-only
        The output ports. An integer ``N`` creates ``N`` ports named
        ``"output-0"`` through ``"output-(N-1)"``. A sequence of names creates
        ports with those exact names. Must specify at least one output.

    Notes
    -----
    Events handled:

    - Events matching one of ``routed_event_types``: routed to one downstream.
    - Events matching one of ``broadcast_event_types``: broadcast to every
      downstream.
    - Other events: rejected at graph build time.
    - End of input: broadcast to every downstream.

    See Also
    --------
    :cpp:`tcspc::route`
        The underlying C++ factory function.
    :py:obj:`Broadcast`
        Sends every event to every downstream.
    """

    def __init__(
        self,
        *routed_event_types: EventType,
        broadcast_event_types: Sequence[EventType] = (),
        router: Router,
        outputs: int | Sequence[str],
    ) -> None:
        if isinstance(outputs, int):
            if outputs < 1:
                raise ValueError("Route requires at least one output")
            output_names: tuple[str, ...] = tuple(
                f"output-{i}" for i in range(outputs)
            )
        else:
            output_names = tuple(outputs)
            if len(output_names) < 1:
                raise ValueError("Route requires at least one output")
            if len(set(output_names)) != len(output_names):
                raise ValueError("Route output names must be unique")
        super().__init__(output=output_names)
        self._routed = tuple(routed_event_types)
        self._broadcast = tuple(broadcast_event_types)
        self._router = router

        for b in self._broadcast:
            if _cpp_utils._contains_type(
                (r._cpp_type_name() for r in self._routed),
                b._cpp_type_name(),
            ):
                raise ValueError(
                    f"event type {b} must not be both routed and broadcast"
                )

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 1:
            raise ValueError(
                f"wrong number of inputs (1 expected, {len(input_event_sets)} found)"
            )
        allowed = self._routed + self._broadcast
        _check_events_subset_of(
            input_event_sets[0], allowed, self.__class__.__name__
        )
        return tuple(allowed for _ in self.outputs())

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._router._parameters()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._router._param_encoders()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        n = len(self.outputs())
        if len(downstreams) != n:
            raise ValueError(
                f"expected {n} downstream(s); found {len(downstreams)}"
            )
        routed = _make_type_list(self._routed)
        broadcast = _make_type_list(self._broadcast)
        router_expr = self._router._cpp_expression(gencontext)
        args = ", ".join([router_expr, *downstreams])
        return _CppExpression(f"tcspc::route<{routed}, {broadcast}>({args})")


def _input_port_names(
    inputs: int | Sequence[str], cls_name: str
) -> tuple[str, ...]:
    if isinstance(inputs, int):
        if inputs < 2:
            raise ValueError(f"{cls_name} requires at least two inputs")
        return tuple(f"input-{i}" for i in range(inputs))
    names = tuple(inputs)
    if len(names) < 2:
        raise ValueError(f"{cls_name} requires at least two inputs")
    if len(set(names)) != len(names):
        raise ValueError(f"{cls_name} input names must be unique")
    return names


@final
class Merge(Node):
    """Merges two sorted input streams into one, ordered by ``abstime``.

    Events arriving on the two inputs are buffered and emitted in
    non-decreasing ``abstime`` order. For events with equal ``abstime``, those
    from ``input-0`` are emitted before those from ``input-1`` (a guaranteed
    tie-breaking order).

    Parameters
    ----------
    *event_types : EventType
        The event types to merge. Every event handled must carry an
        ``abstime`` member; the merge buffers and sorts by it.
    max_buffered : int or Param[int]
        The maximum number of events to buffer from a single input before the
        other input must produce an event. Default: 65536.
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the merge (in particular the
        ``abstime_type``). ``None`` (the default) uses `NumericTraits`
        defaults.

    Notes
    -----
    The node has two input ports, ``"input-0"`` and ``"input-1"``, and a single
    output port. Both inputs must be fed internally (for example by a `Route`
    or `Broadcast` that fans out a single external input); a merge cannot serve
    as the executable graph's external input.

    Events handled:

    - Events matching one of ``event_types``: buffered and emitted in
      non-decreasing ``abstime`` order.
    - Events not in ``event_types``: rejected at graph build time.
    - End of input: flushes any buffered events, then propagates.

    See Also
    --------
    :cpp:`tcspc::merge`
        The underlying C++ factory function.
    :py:obj:`MergeN`
        N-way sorted merge without the equal-``abstime`` tie-breaking
        guarantee.
    :py:obj:`MergeNUnsorted`
        N-way pass-through merge that does no sorting or buffering.
    """

    def __init__(
        self,
        *event_types: EventType,
        max_buffered: int | Param[int] = 65536,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        super().__init__(input=("input-0", "input-1"))
        self._event_types = tuple(event_types)
        self._max_buffered = max_buffered
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 2:
            raise ValueError(
                f"wrong number of inputs (2 expected, {len(input_event_sets)} found)"
            )
        for input_set in input_event_sets:
            _check_events_subset_of(
                input_set, self._event_types, self.__class__.__name__
            )
        return (self._event_types,)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._max_buffered, Param):
            return ((self._max_buffered, _size_type),)
        return ()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        if len(downstreams) != 1:
            raise ValueError(
                f"expected 1 downstream; found {len(downstreams)}"
            )
        type_list = _make_type_list(self._event_types)
        nt = self._numeric_traits._cpp_type_name()
        max_buf = gencontext.size_t_expression(self._max_buffered)
        return _CppExpression(
            f"""\
            tcspc::merge<{type_list}, {nt}>(
                tcspc::arg::max_buffered<std::size_t>{{{max_buf}}}, {downstreams[0]})"""
        )


@final
class MergeN(Node):
    """Merges N sorted input streams into one, ordered by ``abstime``.

    Events arriving on the inputs are buffered and emitted in non-decreasing
    ``abstime`` order. Unlike `Merge`, the relative order of events with equal
    ``abstime`` is **not** guaranteed.

    Parameters
    ----------
    inputs : int or Sequence[str]
        The input ports. An integer ``N`` creates ``N`` ports named
        ``"input-0"`` through ``"input-(N-1)"``. A sequence of names creates
        ports with those exact names. Must specify at least two inputs.
    *event_types : EventType
        The event types to merge. Every event handled must carry an
        ``abstime`` member; the merge buffers and sorts by it.
    max_buffered : int or Param[int]
        The maximum number of events to buffer from a single input before
        another input must produce an event. Default: 65536.
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the merge (in particular the
        ``abstime_type``). ``None`` (the default) uses `NumericTraits`
        defaults.

    Notes
    -----
    All inputs must be fed internally (for example by a `Route` or `Broadcast`
    that fans out a single external input); a merge cannot serve as the
    executable graph's external input.

    Events handled:

    - Events matching one of ``event_types``: buffered and emitted in
      non-decreasing ``abstime`` order.
    - Events not in ``event_types``: rejected at graph build time.
    - End of input: flushes any buffered events, then propagates.

    See Also
    --------
    :cpp:`tcspc::merge_n`
        The underlying C++ factory function.
    :py:obj:`Merge`
        2-way sorted merge that guarantees ``input-0`` precedes ``input-1`` on
        equal ``abstime``.
    :py:obj:`MergeNUnsorted`
        N-way pass-through merge that does no sorting or buffering.
    """

    def __init__(
        self,
        inputs: int | Sequence[str],
        *event_types: EventType,
        max_buffered: int | Param[int] = 65536,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        super().__init__(
            input=_input_port_names(inputs, self.__class__.__name__)
        )
        self._event_types = tuple(event_types)
        self._max_buffered = max_buffered
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        n = len(self.inputs())
        if len(input_event_sets) != n:
            raise ValueError(
                f"wrong number of inputs ({n} expected, {len(input_event_sets)} found)"
            )
        for input_set in input_event_sets:
            _check_events_subset_of(
                input_set, self._event_types, self.__class__.__name__
            )
        return (self._event_types,)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._max_buffered, Param):
            return ((self._max_buffered, _size_type),)
        return ()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        if len(downstreams) != 1:
            raise ValueError(
                f"expected 1 downstream; found {len(downstreams)}"
            )
        n = len(self.inputs())
        type_list = _make_type_list(self._event_types)
        nt = self._numeric_traits._cpp_type_name()
        max_buf = gencontext.size_t_expression(self._max_buffered)
        return _CppExpression(
            f"""\
            tcspc::merge_n<{n}, {type_list}, {nt}>(
                tcspc::arg::max_buffered<std::size_t>{{{max_buf}}}, {downstreams[0]})"""
        )


@final
class MergeNUnsorted(Node):
    """Merges N input streams by passing events through in arrival order.

    Events arriving on any input are emitted immediately, in the order they
    arrive, with no sorting and no buffering. Use this when the inputs are not
    individually sorted, or when ordering does not matter.

    Parameters
    ----------
    inputs : int or Sequence[str]
        The input ports. An integer ``N`` creates ``N`` ports named
        ``"input-0"`` through ``"input-(N-1)"``. A sequence of names creates
        ports with those exact names. Must specify at least two inputs.

    Notes
    -----
    All inputs must be fed internally (for example by a `Route` or `Broadcast`
    that fans out a single external input); a merge cannot serve as the
    executable graph's external input.

    Events handled:

    - All event types: passed through in arrival order.
    - End of input: propagated once all inputs have ended.

    See Also
    --------
    :cpp:`tcspc::merge_n_unsorted`
        The underlying C++ factory function.
    :py:obj:`Merge`
        2-way sorted merge ordered by ``abstime``.
    :py:obj:`MergeN`
        N-way sorted merge ordered by ``abstime``.
    """

    def __init__(self, inputs: int | Sequence[str]) -> None:
        super().__init__(
            input=_input_port_names(inputs, self.__class__.__name__)
        )

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        n = len(self.inputs())
        if len(input_event_sets) != n:
            raise ValueError(
                f"wrong number of inputs ({n} expected, {len(input_event_sets)} found)"
            )
        seen: dict[str, EventType] = {}
        for input_set in input_event_sets:
            for t in input_set:
                seen.setdefault(str(t._cpp_type_name()), t)
        return (tuple(seen.values()),)

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        if len(downstreams) != 1:
            raise ValueError(
                f"expected 1 downstream; found {len(downstreams)}"
            )
        n = len(self.inputs())
        return _CppExpression(
            f"tcspc::merge_n_unsorted<{n}>({downstreams[0]})"
        )


@final
class SinkAll(Node):
    """Sink that discards every event it receives.

    Notes
    -----
    Events handled:

    - All event types: ignored.
    - End of input: ignored.

    See Also
    --------
    :cpp:`tcspc::sink_all`
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
        return _CppExpression("tcspc::sink_all()")


@final
class SourceNothing(_RelayNode):
    """Source processor that emits no events.

    Notes
    -----
    Events handled:

    - This processor has no input events; it is a source.
    - Emits no events.
    - End of input: pass through (flushes the downstream once).

    See Also
    --------
    :cpp:`tcspc::source_nothing`
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
        return _CppExpression(f"tcspc::source_nothing({downstream})")


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
class RecoverOrder(_TypePreservingRelayNode):
    """Pass-through processor that sorts events by abstime within a time window.

    Buffers events and emits them in non-decreasing ``abstime`` order
    once each is at least ``time_window`` behind the latest received
    event. If an event arrives that is older than the latest by more
    than ``time_window``, an error is raised.

    Parameters
    ----------
    time_window : int or Param[int]
        Maximum abstime range over which events may be out of order.
        Must be non-negative.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - All event types with an ``abstime`` field: buffered and forwarded
      in ``abstime`` order once ``time_window`` has elapsed.
    - End of input: emit any buffered events in ``abstime`` order; pass
      through.

    See Also
    --------
    :cpp:`tcspc::recover_order`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        time_window: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._time_window = time_window
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._time_window, Param):
            return ((self._time_window, _int64_type),)
        return ()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        self._sorted_events = tuple(input_event_set)
        return self._sorted_events

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        abstime_t = f"{self._numeric_traits._cpp_type_name()}::abstime_type"
        if isinstance(self._time_window, Param):
            value_expr = f"{gencontext.params_varname}.{self._time_window._cpp_identifier()}"
        else:
            value_expr = f"tcspc::i64{{{self._time_window}LL}}"
        event_list = _make_type_list(self._sorted_events)
        return _CppExpression(
            f"""\
            tcspc::recover_order<
                {event_list},
                {self._numeric_traits._cpp_type_name()}
            >(
                tcspc::arg::time_window<{abstime_t}>{{static_cast<{abstime_t}>({value_expr})}},
                {downstream}
            )"""
        )


@final
class RegulateTimeReached(_TypePreservingRelayNode):
    """Processor that regulates the frequency of `TimeReachedEvent`.

    Ensures that the event stream contains `TimeReachedEvent` at
    reasonable abstime intervals and event-count intervals, removing
    redundant ones based on the same criteria. Useful when sorting
    events from multiple streams by abstime downstream.

    Parameters
    ----------
    interval_threshold : int or Param[int]
        A `TimeReachedEvent` is emitted at the next opportunity if at
        least this abstime interval has elapsed since the previously
        emitted one. Use the maximum value of the abstime type to
        effectively disable the interval criterion.
    count_threshold : int or Param[int]
        A `TimeReachedEvent` is emitted when this many events have
        been emitted since the previously emitted one.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `TimeReachedEvent`: emit, possibly rate-limited.
    - All event types with an ``abstime`` field: pass through, possibly
      followed by a `TimeReachedEvent`.
    - End of input: emit a final `TimeReachedEvent` with the time of
      the last event passed; pass through.

    See Also
    --------
    :cpp:`tcspc::regulate_time_reached`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        interval_threshold: int | Param[int],
        count_threshold: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._interval = interval_threshold
        self._count = count_threshold
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._interval, Param):
            params.append((self._interval, _int64_type))
        if isinstance(self._count, Param):
            params.append((self._count, _size_type))
        return params

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        abstime_t = f"{self._numeric_traits._cpp_type_name()}::abstime_type"
        if isinstance(self._interval, Param):
            interval_expr = f"{gencontext.params_varname}.{self._interval._cpp_identifier()}"
        else:
            interval_expr = f"tcspc::i64{{{self._interval}LL}}"
        count_expr = gencontext.size_t_expression(self._count)
        return _CppExpression(
            f"""\
            tcspc::regulate_time_reached<{self._numeric_traits._cpp_type_name()}>(
                tcspc::arg::interval_threshold<{abstime_t}>{{static_cast<{abstime_t}>({interval_expr})}},
                tcspc::arg::count_threshold<std::size_t>{{{count_expr}}},
                {downstream}
            )"""
        )


@final
class RemoveTimeCorrelation(_RelayNode):
    """Processor that strips the difftime from time-correlated detection events.

    Converts each `TimeCorrelatedDetectionEvent` into a `DetectionEvent`
    (with the same ``abstime`` and ``channel``); other event types pass
    through unchanged.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime`` and ``channel`` types.
        Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `TimeCorrelatedDetectionEvent`: emit a `DetectionEvent` with the
      same ``abstime`` and ``channel``.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::remove_time_correlation`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        tcd = _events.TimeCorrelatedDetectionEvent(self._numeric_traits)
        det = _events.DetectionEvent(self._numeric_traits)
        out = tuple(t for t in input_event_set if t != tcd)
        return (*out, det)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::remove_time_correlation<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class Select(_RelayNode):
    """Processor that passes only events of the listed types, dropping others.

    Parameters
    ----------
    *event_types : EventType
        The event types to pass through. Events of all other types are
        silently discarded.

    Notes
    -----
    Events handled:

    - Events in ``event_types``: pass through unchanged.
    - All other event types: discarded.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::select`
        The underlying C++ factory function.
    :py:obj:`SelectAll`
        Forward every event unchanged.
    :py:obj:`SelectExcept`
        Discard the listed types, passing others.
    """

    def __init__(self, *event_types: EventType) -> None:
        self._event_types = tuple(event_types)

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return tuple(
            t
            for t in input_event_set
            if _cpp_utils._contains_type(
                (u._cpp_type_name() for u in self._event_types),
                t._cpp_type_name(),
            )
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::select<
                {_make_type_list(self._event_types)}
            >(
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
class SelectExcept(_RelayNode):
    """Processor that discards events of the listed types, passing others.

    The complement of `Select`.

    Parameters
    ----------
    *event_types : EventType
        The event types to discard. Events of all other types pass
        through unchanged.

    Notes
    -----
    Events handled:

    - Events in ``event_types``: discarded.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::select_except`
        The underlying C++ factory function.
    :py:obj:`Select`
        Pass only the listed types, dropping others.
    :py:obj:`SelectAll`
        Forward every event unchanged.
    """

    def __init__(self, *event_types: EventType) -> None:
        self._event_types = tuple(event_types)

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::select_except<
                {_make_type_list(self._event_types)}
            >(
                {downstream}
            )"""
        )


@final
class SinkOnly(Node):
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
    :cpp:`tcspc::sink_only`
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
        return _CppExpression(f"tcspc::sink_only<{evts}>()")


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
class RebaseAbstime(_TypePreservingRelayNode):
    """Pass-through processor that shifts abstime so the first event is at zero.

    Subtracts the abstime of the first event seen from every event's
    abstime, so that downstream processing sees an abstime starting at
    zero. Wrap-around is handled correctly even if ``abstime_type`` is
    a signed integer type.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - All event types with an ``abstime`` field: pass through with
      ``abstime`` made relative to the first event.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::rebase_abstime`
        The underlying C++ factory function.
    :py:obj:`Delay`
        Offset event abstimes by a constant delta.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::rebase_abstime<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )
