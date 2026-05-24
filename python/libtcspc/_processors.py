# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Collection, Iterable, Mapping, Sequence
from typing import Any, final

from typing_extensions import override

from . import _access, _cpp_utils, _events, _streams
from ._access import AccessTag, _AccessSpec
from ._acquisition_readers import AcquisitionReader, PyAcquisitionReader
from ._bin_mappers import BinMapper
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
from ._data_mappers import DataMapper
from ._events import BucketEvent, EventType, WarningEvent
from ._graph import Graph, Subgraph
from ._matchers import Matcher
from ._node import Node, _RelayNode, _TypePreservingRelayNode
from ._numeric_traits import NumericTraits
from ._param import Param
from ._routers import Router
from ._timing_generators import TimingGenerator


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


def _with_event_added(
    input_events: Iterable[EventType], event: EventType
) -> tuple[EventType, ...]:
    events = tuple(input_events)
    if _cpp_utils._contains_type(
        (t._cpp_type_name() for t in events), event._cpp_type_name()
    ):
        return events
    return (*events, event)


def _double_expr(
    gencontext: _CodeGenerationContext, v: float | Param[float]
) -> str:
    if isinstance(v, Param):
        return f"{gencontext.params_varname}.{v._cpp_identifier()}"
    return repr(float(v))


def _cast_int_expr(
    gencontext: _CodeGenerationContext,
    v: int | Param[int],
    cpp_type: str,
) -> str:
    if isinstance(v, Param):
        inner = f"{gencontext.params_varname}.{v._cpp_identifier()}"
    else:
        inner = str(v)
    return f"static_cast<{cpp_type}>({inner})"


_OVERFLOW_POLICIES = {
    "error": "tcspc::histogram_policy::error_on_overflow",
    "stop": "tcspc::histogram_policy::stop_on_overflow",
    "saturate": "tcspc::histogram_policy::saturate_on_overflow",
    "reset": "tcspc::histogram_policy::reset_on_overflow",
}


def _histogram_policy_expression(
    overflow: str,
    *,
    emit_concluding: bool = False,
    reset_after_scan: bool = False,
    clear_every_scan: bool = False,
    no_clear_new_bucket: bool = False,
) -> str:
    if overflow not in _OVERFLOW_POLICIES:
        raise ValueError(
            f"overflow must be one of {sorted(_OVERFLOW_POLICIES)}"
        )
    parts = [_OVERFLOW_POLICIES[overflow]]
    if emit_concluding:
        parts.append("tcspc::histogram_policy::emit_concluding_events")
    if reset_after_scan:
        parts.append("tcspc::histogram_policy::reset_after_scan")
    if clear_every_scan:
        parts.append("tcspc::histogram_policy::clear_every_scan")
    if no_clear_new_bucket:
        parts.append("tcspc::histogram_policy::no_clear_new_bucket")
    return "(" + " | ".join(parts) + ")"


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
class AddCountToPeriodicSequences(_RelayNode):
    """Processor that converts periodic sequence model events to linear timing events.

    Converts each `PeriodicSequenceModelEvent` to a `RealLinearTimingEvent`
    with the given count. Other events pass through.

    Parameters
    ----------
    count : int or Param[int]
        Number of ticks in the generated linear timing.
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted event. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::add_count_to_periodic_sequences`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        count: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._count = count
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._count, Param):
            return ((self._count, _size_type),)
        return ()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        model = _events.PeriodicSequenceModelEvent(self._numeric_traits)
        linear = _events.RealLinearTimingEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (model,))
        return _with_event_added(out, linear)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        count = gencontext.size_t_expression(self._count)
        return _CppExpression(
            f"""\
            tcspc::add_count_to_periodic_sequences<{self._numeric_traits._cpp_type_name()}>(
                tcspc::arg::count<std::size_t>{{{count}}},
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
class BatchBinIncrementClusters(_RelayNode):
    """Processor that collects bin increment clusters into encoded batches.

    An optimized analogue of `Batch` for `BinIncrementClusterEvent`. Each
    completed batch is emitted as a `BucketEvent` of bin indices. Must be paired
    with `UnbatchBinIncrementClusters` downstream to recover the clusters.

    Parameters
    ----------
    bucket_size : int or Param[int]
        Size (in elements) of each storage bucket.
    batch_size : int or Param[int]
        Number of clusters per batch.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets. If ``None``, a default `RecyclingBucketSource` is
        used.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``bin_index_type``. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BinIncrementClusterEvent`: encoded into a batch; emitted when full.
    - All other event types: pass through unchanged.
    - End of input: emit any partial batch, then pass through.

    See Also
    --------
    :cpp:`tcspc::batch_bin_increment_clusters`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        bucket_size: int | Param[int],
        batch_size: int | Param[int],
        *,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._bucket_size = bucket_size
        self._batch_size = batch_size
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )
        self._index_element = _events._TraitsMemberEvent(
            self._numeric_traits, "bin_index_type"
        )
        self._bucket_source = _bucket_source_or_default(
            self._index_element, buffer_provider
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._bucket_size, Param):
            params.append((self._bucket_size, _size_type))
        if isinstance(self._batch_size, Param):
            params.append((self._batch_size, _size_type))
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._bucket_source._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        cluster = _events.BinIncrementClusterEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (cluster,))
        return _with_event_added(out, BucketEvent(self._index_element))

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        bucket_size = gencontext.size_t_expression(self._bucket_size)
        batch_size = gencontext.size_t_expression(self._batch_size)
        return _CppExpression(
            f"""\
            tcspc::batch_bin_increment_clusters<{self._numeric_traits._cpp_type_name()}>(
                {self._bucket_source._cpp_expression(gencontext)},
                tcspc::arg::bucket_size<std::size_t>{{{bucket_size}}},
                tcspc::arg::batch_size<std::size_t>{{{batch_size}}},
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
class ClusterBinIncrements(_RelayNode):
    """Processor that collects bin increments between start and stop events into clusters.

    Bin increments received while within a cluster (after a ``start_event_type``
    and before a ``stop_event_type``) are collected; each completed cluster is
    emitted as a `BinIncrementClusterEvent`.

    Parameters
    ----------
    start_event_type : EventType
        Event type that starts a cluster.
    stop_event_type : EventType
        Event type that ends a cluster.
    numeric_traits : NumericTraits or None
        Numeric traits for the events. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - ``start_event_type``: begin a new cluster (consumed).
    - ``stop_event_type``: emit the current cluster as a
      `BinIncrementClusterEvent` (consumed).
    - `BinIncrementEvent`: recorded into the current cluster (consumed).
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::cluster_bin_increments`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        start_event_type: EventType,
        stop_event_type: EventType,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._start = start_event_type
        self._stop = stop_event_type
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        bi = _events.BinIncrementEvent(self._numeric_traits)
        cluster = _events.BinIncrementClusterEvent(self._numeric_traits)
        out = _remove_events_from_set(
            input_event_set, (bi, self._start, self._stop)
        )
        return _with_event_added(out, cluster)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::cluster_bin_increments<
                {self._start._cpp_type_name()},
                {self._stop._cpp_type_name()},
                {self._numeric_traits._cpp_type_name()}
            >(
                {downstream}
            )"""
        )


@final
class ConvertSequencesToStartStop(_RelayNode):
    """Processor that converts tick sequences to gapless start-stop event pairs.

    Every ``count + 1`` ``tick_event_type`` events are replaced by a series of
    ``start_event_type`` and ``stop_event_type`` events bracketing each tick
    interval. Other events pass through.

    Parameters
    ----------
    tick_event_type : EventType
        The tick event type (consumed). Must have an ``abstime`` field.
    start_event_type : EventType
        The emitted start event type. Must be brace-initializable and have an
        ``abstime`` field.
    stop_event_type : EventType
        The emitted stop event type. Must be brace-initializable and have an
        ``abstime`` field.
    count : int or Param[int]
        Number of intervals per sequence.

    See Also
    --------
    :cpp:`tcspc::convert_sequences_to_start_stop`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        tick_event_type: EventType,
        start_event_type: EventType,
        stop_event_type: EventType,
        count: int | Param[int],
    ) -> None:
        self._tick = tick_event_type
        self._start = start_event_type
        self._stop = stop_event_type
        self._count = count

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._count, Param):
            return ((self._count, _size_type),)
        return ()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        out = _remove_events_from_set(input_event_set, (self._tick,))
        out = _with_event_added(out, self._start)
        return _with_event_added(out, self._stop)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        count = gencontext.size_t_expression(self._count)
        return _CppExpression(
            f"""\
            tcspc::convert_sequences_to_start_stop<
                {self._tick._cpp_type_name()},
                {self._start._cpp_type_name()},
                {self._stop._cpp_type_name()}
            >(
                tcspc::arg::count<std::size_t>{{{count}}},
                {downstream}
            )"""
        )


@final
class CopyToBuckets(_RelayNode):
    """Processor that copies pushed data into buckets.

    Used to plug a push-style data source — such as a hardware acquisition
    driver that hands the application blocks of data — into a libtcspc
    processing graph. The contents of each incoming data event are copied
    into a bucket obtained from the buffer provider, which is then emitted
    as a `BucketEvent` downstream. This is the push-mode counterpart of
    `Acquire`.

    Data is typically fed in by pushing numpy arrays (or other
    buffer-protocol objects) into the graph input; each is wrapped
    zero-copy as a read-only bucket, so the only bulk-data copy is the one
    into the bucket-source's buckets.

    Parameters
    ----------
    element_type : EventType
        Element type ``T`` of the data. Each emitted bucket holds a
        contiguous array of this type.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets used to hold each copy. If ``None``, a default
        `RecyclingBucketSource` for ``element_type`` is used. A runtime
        `Param` of type `PyBucketSource` binds a Python bucket source at
        execution time.

    Notes
    -----
    Events handled:

    - A `ConstBucketEvent` of ``element_type`` (a read-only ``bucket<T
      const>``): copied into a bucket and emitted as a `BucketEvent` of
      ``element_type`` (variable size, one per incoming data event).
    - All other event types: passed through unchanged, to carry out-of-band
      timing events.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::copy_to_buckets`
        The underlying C++ factory function.
    :py:obj:`CopyToFullBuckets`
        The two-output counterpart that also delivers real-time views.
    :py:obj:`Acquire`
        The pull-mode counterpart.
    :py:obj:`BucketSource`
        Interface for bucket sources.
    :py:obj:`PyBucketSource`
        Interface for Python bucket sources (used via `Param`).
    """

    def __init__(
        self,
        element_type: EventType,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
    ) -> None:
        self._element_type = element_type
        self._data_event = _events.ConstBucketEvent(element_type)
        self._bucket_source = _bucket_source_or_default(
            element_type, buffer_provider
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return tuple(self._bucket_source._parameters())

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        passthrough = _remove_events_from_set(
            input_event_set,
            (self._data_event, _events.BucketEvent(self._element_type)),
        )
        return (_events.BucketEvent(self._element_type), *passthrough)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::copy_to_buckets<{self._data_event._cpp_type_name()}, {self._element_type._cpp_type_name()}>(
                {self._bucket_source._cpp_expression(gencontext)},
                {downstream}
            )"""
        )


@final
class CopyToFullBuckets(Node):
    """Processor that copies pushed data into fixed-size buckets while also delivering real-time read-only views.

    Like `CopyToBuckets`, this plugs a push-style data source into a
    libtcspc processing graph by copying the contents of each incoming data
    event into buckets obtained from the buffer provider. Unlike
    `CopyToBuckets`, it collects the data into fixed-size buckets and has
    two outputs:

    - ``live``: a read-only `ConstBucketEvent` view of the data copied on
      each incoming event, delivered immediately (typically used for live
      display).
    - ``batch``: a full `BucketEvent` bucket emitted whenever ``batch_size``
      elements have been collected, plus any partial final bucket on flush
      (typically used for saving to disk).

    This is the push-mode counterpart of `AcquireFullBuckets`. Data is
    typically fed in by pushing numpy arrays (or other buffer-protocol
    objects) into the graph input; each is wrapped zero-copy as a read-only
    bucket, so the only bulk-data copy is the one into the bucket-source's
    buckets. The ``live`` views are zero-copy when the buffer provider
    supplies shared-view-capable buckets, so they must not be modified.

    Parameters
    ----------
    element_type : EventType
        Element type ``T`` of the data. Each emitted bucket holds a
        contiguous array of this type.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets used to hold each batch. If ``None``, a default
        `RecyclingBucketSource` for ``element_type`` is used. A runtime
        `Param` of type `PyBucketSource` binds a Python bucket source at
        execution time. The buffer provider must support shared views unless
        the ``live`` output is connected to a `SinkAll`.
    batch_size : int or Param[int] or None
        Number of elements collected in each full bucket. Smaller values
        reduce latency; larger values reduce per-batch overhead. Defaults to
        ``65536``. Must be positive.

    Notes
    -----
    Events handled:

    - A `ConstBucketEvent` of ``element_type`` (a read-only ``bucket<T
      const>``): copied into the current bucket. A read-only
      `ConstBucketEvent` view of the copy is emitted on ``live``, and a full
      `BucketEvent` of ``element_type`` is emitted on ``batch`` whenever
      ``batch_size`` elements have been collected.
    - All other event types: passed through unchanged on ``live``, to carry
      out-of-band timing events.
    - End of input: any partial bucket is emitted on ``batch`` before both
      downstreams are flushed.

    See Also
    --------
    :cpp:`tcspc::copy_to_full_buckets`
        The underlying C++ factory function.
    :py:obj:`CopyToBuckets`
        The single-output counterpart.
    :py:obj:`AcquireFullBuckets`
        The pull-mode counterpart.
    :py:obj:`BucketSource`
        Interface for bucket sources.
    :py:obj:`PyBucketSource`
        Interface for Python bucket sources (used via `Param`).
    """

    def __init__(
        self,
        element_type: EventType,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
        batch_size: int | Param[int] | None = None,
    ) -> None:
        super().__init__(output=("live", "batch"))
        self._element_type = element_type
        self._data_event = _events.ConstBucketEvent(element_type)
        self._bucket_source = _bucket_source_or_default(
            element_type, buffer_provider
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
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 1:
            raise ValueError(
                f"wrong number of inputs (1 expected, {len(input_event_sets)} found)"
            )
        passthrough = _remove_events_from_set(
            input_event_sets[0],
            (self._data_event, _events.ConstBucketEvent(self._element_type)),
        )
        live = (_events.ConstBucketEvent(self._element_type), *passthrough)
        batch = (_events.BucketEvent(self._element_type),)
        return (live, batch)

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
        batch_size = gencontext.size_t_expression(self._batch_size)
        return _CppExpression(
            f"""\
            tcspc::copy_to_full_buckets<{self._data_event._cpp_type_name()}, {self._element_type._cpp_type_name()}>(
                {self._bucket_source._cpp_expression(gencontext)},
                tcspc::arg::batch_size{{{batch_size}}},
                {live},
                {batch}
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
class ExtractBucket(_RelayNode):
    """Processor that extracts the bucket carried by a bucket-carrying event.

    Pulls the ``data_bucket`` field out of each ``event_type`` and emits it as
    a `BucketEvent`. This is the way to obtain the result of `Histogram` or
    `ScanHistograms` (whose output events cannot be sent to a Python sink
    directly) as a NumPy array.

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
class ExtrapolatePeriodicSequences(_RelayNode):
    """Processor that extrapolates a periodic sequence to a one-shot timing event.

    Converts each `PeriodicSequenceModelEvent` to a `RealOneShotTimingEvent`
    extrapolated to the given tick index. Other events pass through.

    Parameters
    ----------
    tick_index : int or Param[int]
        Tick index to extrapolate to.
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted event. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::extrapolate_periodic_sequences`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        tick_index: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._tick_index = tick_index
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._tick_index, Param):
            return ((self._tick_index, _size_type),)
        return ()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        model = _events.PeriodicSequenceModelEvent(self._numeric_traits)
        one_shot = _events.RealOneShotTimingEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (model,))
        return _with_event_added(out, one_shot)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        tick_index = gencontext.size_t_expression(self._tick_index)
        return _CppExpression(
            f"""\
            tcspc::extrapolate_periodic_sequences<{self._numeric_traits._cpp_type_name()}>(
                tcspc::arg::tick_index<std::size_t>{{{tick_index}}},
                {downstream}
            )"""
        )


@final
class FitPeriodicSequences(_RelayNode):
    """Processor that fits fixed-length periodic sequences and estimates timing.

    Every ``length`` events of ``event_type`` are fit to a periodic model; a
    `PeriodicSequenceModelEvent` is emitted with the estimated start time and
    interval. Other events pass through.

    Parameters
    ----------
    event_type : EventType
        The event type to accumulate and fit. Must have an ``abstime`` field.
    length : int or Param[int]
        Number of events per sequence (at least 3).
    min_interval : float or Param[float]
        Minimum acceptable estimated interval.
    max_interval : float or Param[float]
        Maximum acceptable estimated interval.
    max_mse : float or Param[float]
        Maximum acceptable mean squared error of the fit.
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted event. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::fit_periodic_sequences`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_type: EventType,
        length: int | Param[int],
        min_interval: float | Param[float],
        max_interval: float | Param[float],
        max_mse: float | Param[float],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._event_type = event_type
        self._length = length
        self._min_interval = min_interval
        self._max_interval = max_interval
        self._max_mse = max_mse
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._length, Param):
            params.append((self._length, _size_type))
        for v in (self._min_interval, self._max_interval, self._max_mse):
            if isinstance(v, Param):
                params.append((v, _CppTypeName("double")))
        return params

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        model = _events.PeriodicSequenceModelEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (self._event_type,))
        return _with_event_added(out, model)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        length = gencontext.size_t_expression(self._length)
        return _CppExpression(
            f"""\
            tcspc::fit_periodic_sequences<
                {self._event_type._cpp_type_name()}, {self._numeric_traits._cpp_type_name()}
            >(
                tcspc::arg::length<std::size_t>{{{length}}},
                tcspc::arg::min_interval<double>{{{_double_expr(gencontext, self._min_interval)}}},
                tcspc::arg::max_interval<double>{{{_double_expr(gencontext, self._max_interval)}}},
                tcspc::arg::max_mse<double>{{{_double_expr(gencontext, self._max_mse)}}},
                {downstream}
            )"""
        )


@final
class Gate(_TypePreservingRelayNode):
    """Pass-through processor that gates events depending on open/close state.

    Maintains a boolean gate. When an ``open_event_type`` is received the gate
    opens; when a ``close_event_type`` is received it closes. Events of the
    gated types are passed through only while the gate is open; all other
    events (including the open and close events) always pass through.

    Parameters
    ----------
    *gated_event_types : EventType
        Event types to gate.
    open_event_type : EventType, keyword-only
        Event type that opens the gate.
    close_event_type : EventType, keyword-only
        Event type that closes the gate.
    initially_open : bool or Param[bool], keyword-only
        Initial state of the gate. Default ``False``.

    Notes
    -----
    Events handled:

    - Events in ``gated_event_types``: passed through only while open.
    - ``open_event_type`` / ``close_event_type``: open/close the gate and pass
      through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::gate`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        *gated_event_types: EventType,
        open_event_type: EventType,
        close_event_type: EventType,
        initially_open: bool | Param[bool] = False,
    ) -> None:
        self._gated = tuple(gated_event_types)
        self._open = open_event_type
        self._close = close_event_type
        self._initially_open = initially_open

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._initially_open, Param):
            return ((self._initially_open, _CppTypeName("bool")),)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        gated_list = _make_type_list(self._gated)
        if isinstance(self._initially_open, Param):
            value = f"{gencontext.params_varname}.{self._initially_open._cpp_identifier()}"
        else:
            value = "true" if self._initially_open else "false"
        return _CppExpression(
            f"""\
            tcspc::gate<
                {gated_list},
                {self._open._cpp_type_name()},
                {self._close._cpp_type_name()}
            >(
                tcspc::arg::initially_open{{static_cast<bool>({value})}},
                {downstream}
            )"""
        )


@final
class Generate(_RelayNode):
    """Processor that generates timing events in response to a trigger.

    Each ``trigger_event_type`` starts generation of a pattern of
    ``output_event_type`` events according to the timing generator. All input
    events pass through; generated events are interleaved by abstime.

    Parameters
    ----------
    trigger_event_type : EventType
        The event type that triggers generation.
    output_event_type : EventType
        The event type generated. Must have an ``abstime`` field.
    generator : TimingGenerator
        The timing generator producing the pattern.

    Notes
    -----
    Events handled:

    - ``trigger_event_type``: start generating a pattern; pass through.
    - All other event types: pass through; generated events interleaved.
    - End of input: pass through (remaining timings not generated).

    See Also
    --------
    :cpp:`tcspc::generate`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        trigger_event_type: EventType,
        output_event_type: EventType,
        generator: TimingGenerator,
    ) -> None:
        self._trigger = trigger_event_type
        self._output = output_event_type
        self._generator = generator

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._generator._parameters()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._generator._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _with_event_added(input_event_set, self._output)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        generator = self._generator._cpp_expression(gencontext)
        return _CppExpression(
            f"""\
            tcspc::generate<
                {self._trigger._cpp_type_name()},
                {self._output._cpp_type_name()}
            >(
                {generator},
                {downstream}
            )"""
        )


@final
class Histogram(_RelayNode):
    """Processor that accumulates a histogram from bin increment events.

    Each `BinIncrementEvent` increments the corresponding bin; a `HistogramEvent`
    carrying a view of the current histogram is emitted on each increment. A
    round of accumulation ends on a ``reset_event_type``; if concluding events
    are enabled, a `ConcludingHistogramEvent` is emitted on each reset.

    Parameters
    ----------
    num_bins : int or Param[int]
        Number of bins in the histogram.
    max_per_bin : int or Param[int]
        Maximum count per bin before the overflow policy applies.
    reset_event_type : EventType or None
        Event type that resets (starts a new round). ``None`` (the default)
        means no reset event.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets holding the histogram. If ``None``, a default
        `RecyclingBucketSource` is used.
    overflow : str
        Overflow behavior: ``"error"`` (default), ``"stop"``, ``"saturate"``,
        or ``"reset"``.
    emit_concluding : bool
        If ``True``, emit a `ConcludingHistogramEvent` on each reset. Default
        ``False``.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``bin_index_type`` and ``bin_type``. Defaults
        to ``NumericTraits()``.

    Notes
    -----
    The histogram-carrying output events cannot be sent to a Python sink; insert
    `ExtractBucket` to obtain the histogram as a NumPy array.

    See Also
    --------
    :cpp:`tcspc::histogram`
        The underlying C++ factory function.
    :py:obj:`ExtractBucket`
        Extract the histogram bucket as a NumPy array.
    """

    def __init__(
        self,
        num_bins: int | Param[int],
        max_per_bin: int | Param[int],
        reset_event_type: EventType | None = None,
        *,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
        overflow: str = "error",
        emit_concluding: bool = False,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        if overflow not in _OVERFLOW_POLICIES:
            raise ValueError(
                f"overflow must be one of {sorted(_OVERFLOW_POLICIES)}"
            )
        self._num_bins = num_bins
        self._max_per_bin = max_per_bin
        self._reset_event_type = reset_event_type
        self._overflow = overflow
        self._emit_concluding = emit_concluding
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )
        self._bin_element = _events._TraitsMemberEvent(
            self._numeric_traits, "bin_type"
        )
        self._bucket_source = _bucket_source_or_default(
            self._bin_element, buffer_provider
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        nt = self._numeric_traits._cpp_type_name()
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._num_bins, Param):
            params.append((self._num_bins, _size_type))
        if isinstance(self._max_per_bin, Param):
            params.append((self._max_per_bin, _CppTypeName(f"{nt}::bin_type")))
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._bucket_source._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        bi = _events.BinIncrementEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (bi,))
        out = _with_event_added(
            out, _events.HistogramEvent(self._numeric_traits)
        )
        if self._emit_concluding:
            out = _with_event_added(
                out, _events.ConcludingHistogramEvent(self._numeric_traits)
            )
        if self._overflow == "saturate":
            out = _with_event_added(out, WarningEvent())
        return out

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        bt = f"{nt}::bin_type"
        policy = _histogram_policy_expression(
            self._overflow, emit_concluding=self._emit_concluding
        )
        reset = (
            self._reset_event_type._cpp_type_name()
            if self._reset_event_type is not None
            else "tcspc::never_event"
        )
        num_bins = gencontext.size_t_expression(self._num_bins)
        max_per_bin = _cast_int_expr(gencontext, self._max_per_bin, bt)
        return _CppExpression(
            f"""\
            tcspc::histogram<{policy}, {reset}, {nt}>(
                tcspc::arg::num_bins<std::size_t>{{{num_bins}}},
                tcspc::arg::max_per_bin<{bt}>{{{max_per_bin}}},
                {self._bucket_source._cpp_expression(gencontext)},
                {downstream}
            )"""
        )


@final
class MapToBins(_RelayNode):
    """Processor that maps datapoint events to bin increment events.

    Each `DatapointEvent` is mapped to a `BinIncrementEvent` by the bin mapper
    (or discarded if the bin mapper rejects it); other events pass through.

    Parameters
    ----------
    bin_mapper : BinMapper
        The bin mapper mapping datapoints to bin indices.
    numeric_traits : NumericTraits or None
        Numeric traits for the events. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `DatapointEvent`: emit a `BinIncrementEvent` (unless discarded).
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::map_to_bins`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        bin_mapper: BinMapper,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._bin_mapper = bin_mapper
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _accesses(self) -> Sequence[tuple[AccessTag, type[_AccessSpec]]]:
        return self._bin_mapper._accesses()

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._bin_mapper._parameters()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._bin_mapper._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        dp = _events.DatapointEvent(self._numeric_traits)
        bi = _events.BinIncrementEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (dp,))
        return _with_event_added(out, bi)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        bin_mapper = self._bin_mapper._cpp_expression(gencontext)
        return _CppExpression(
            f"""\
            tcspc::map_to_bins<{self._numeric_traits._cpp_type_name()}>(
                {bin_mapper},
                {downstream}
            )"""
        )


@final
class MapToDatapoints(_RelayNode):
    """Processor that maps events to datapoint events using a data mapper.

    Each event of ``event_type`` is mapped to a `DatapointEvent` by the data
    mapper; other events pass through.

    Parameters
    ----------
    event_type : EventType
        The event type to map.
    data_mapper : DataMapper
        The data mapper extracting the datapoint value.
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted `DatapointEvent`. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: emit a `DatapointEvent`.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::map_to_datapoints`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_type: EventType,
        data_mapper: DataMapper,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._event_type = event_type
        self._data_mapper = data_mapper
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._data_mapper._parameters()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._data_mapper._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        dp = _events.DatapointEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (self._event_type,))
        return _with_event_added(out, dp)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        mapper = self._data_mapper._cpp_expression(gencontext)
        return _CppExpression(
            f"""\
            tcspc::map_to_datapoints<
                {self._event_type._cpp_type_name()}, {self._numeric_traits._cpp_type_name()}
            >(
                {mapper},
                {downstream}
            )"""
        )


class _Match(_RelayNode):
    """Common base for the match relay nodes."""

    _factory = ""

    def __init__(
        self,
        event_type: EventType,
        out_event_type: EventType,
        matcher: Matcher,
    ) -> None:
        self._event_type = event_type
        self._out_event_type = out_event_type
        self._matcher = matcher

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._matcher._parameters()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._matcher._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _with_event_added(input_event_set, self._out_event_type)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        matcher = self._matcher._cpp_expression(gencontext)
        return _CppExpression(
            f"""\
            tcspc::{self._factory}<
                {self._event_type._cpp_type_name()},
                {self._out_event_type._cpp_type_name()}
            >(
                {matcher},
                {downstream}
            )"""
        )


@final
class Match(_Match):
    """Processor that emits an output event for each matched event, passing all through.

    For each event of ``event_type`` that the matcher matches, an
    ``out_event_type`` (constructed with the matched event's ``abstime``) is
    emitted. All input events, matched or not, pass through.

    Parameters
    ----------
    event_type : EventType
        The event type tested by the matcher.
    out_event_type : EventType
        The event type emitted on a match. Must be constructible from an
        ``abstime``.
    matcher : Matcher
        The matcher deciding which events match.

    See Also
    --------
    :cpp:`tcspc::match`
        The underlying C++ factory function.
    :py:obj:`MatchAndConsume`
        Like `Match`, but matched events are not passed through.
    """

    _factory = "match"


@final
class MatchAndConsume(_Match):
    """Processor that emits an output event for each matched event, consuming matches.

    Like `Match`, but matched events are not passed through (consumed); only
    unmatched events of ``event_type`` and other events pass through.

    Parameters
    ----------
    event_type : EventType
        The event type tested by the matcher.
    out_event_type : EventType
        The event type emitted on a match. Must be constructible from an
        ``abstime``.
    matcher : Matcher
        The matcher deciding which events match.

    See Also
    --------
    :cpp:`tcspc::match_and_consume`
        The underlying C++ factory function.
    :py:obj:`Match`
        Like `MatchAndConsume`, but matched events are also passed through.
    """

    _factory = "match_and_consume"


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


class _Pair(_RelayNode):
    """Common base for the pairing relay nodes."""

    _factory = ""

    def __init__(
        self,
        start_channel: int | Param[int],
        stop_channels: Sequence[int],
        time_window: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._start_channel = start_channel
        self._stop_channels = tuple(stop_channels)
        self._time_window = time_window
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        nt = self._numeric_traits._cpp_type_name()
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._start_channel, Param):
            params.append(
                (self._start_channel, _CppTypeName(f"{nt}::channel_type"))
            )
        if isinstance(self._time_window, Param):
            params.append(
                (self._time_window, _CppTypeName(f"{nt}::abstime_type"))
            )
        return params

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        pair = _events.DetectionPairEvent(self._numeric_traits)
        return _with_event_added(input_event_set, pair)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        ct = f"{nt}::channel_type"
        at = f"{nt}::abstime_type"
        n = len(self._stop_channels)
        start = _cast_int_expr(gencontext, self._start_channel, ct)
        window = _cast_int_expr(gencontext, self._time_window, at)
        stop_arr = (
            f"std::array<{ct}, {n}>{{{{"
            + ", ".join(f"static_cast<{ct}>({c})" for c in self._stop_channels)
            + "}}"
        )
        return _CppExpression(
            f"""\
            tcspc::{self._factory}<{n}, {nt}>(
                tcspc::arg::start_channel<{ct}>{{{start}}},
                {stop_arr},
                tcspc::arg::time_window<{at}>{{{window}}},
                {downstream}
            )"""
        )


@final
class PairAll(_Pair):
    """Processor that pairs each start detection with all stops within a window.

    Each detection on a stop channel is paired with every buffered start
    detection (on ``start_channel``) within ``time_window``, emitting a
    `DetectionPairEvent` for each. Detection events also pass through.

    Parameters
    ----------
    start_channel : int or Param[int]
        Channel of the start detections.
    stop_channels : Sequence[int]
        Channels of the stop detections.
    time_window : int or Param[int]
        Maximum abstime difference for a pair (must be non-negative).
    numeric_traits : NumericTraits or None
        Numeric traits for the events. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::pair_all`
        The underlying C++ factory function.
    """

    _factory = "pair_all"


@final
class PairAllBetween(_Pair):
    """Processor that pairs starts with all stops occurring before the next start.

    Like `PairAll`, but only stop detections occurring before the next start
    detection are considered.

    Parameters
    ----------
    start_channel : int or Param[int]
        Channel of the start detections.
    stop_channels : Sequence[int]
        Channels of the stop detections.
    time_window : int or Param[int]
        Maximum abstime difference for a pair (must be non-negative).
    numeric_traits : NumericTraits or None
        Numeric traits for the events. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::pair_all_between`
        The underlying C++ factory function.
    """

    _factory = "pair_all_between"


@final
class PairOne(_Pair):
    """Processor that pairs each start with at most one stop per stop channel.

    Like `PairAll`, but each buffered start is paired with at most one
    detection per stop channel.

    Parameters
    ----------
    start_channel : int or Param[int]
        Channel of the start detections.
    stop_channels : Sequence[int]
        Channels of the stop detections.
    time_window : int or Param[int]
        Maximum abstime difference for a pair (must be non-negative).
    numeric_traits : NumericTraits or None
        Numeric traits for the events. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::pair_one`
        The underlying C++ factory function.
    """

    _factory = "pair_one"


@final
class PairOneBetween(_Pair):
    """Processor that pairs starts with one stop per channel before the next start.

    Like `PairOne`, but only stop detections occurring before the next start
    detection are considered.

    Parameters
    ----------
    start_channel : int or Param[int]
        Channel of the start detections.
    stop_channels : Sequence[int]
        Channels of the stop detections.
    time_window : int or Param[int]
        Maximum abstime difference for a pair (must be non-negative).
    numeric_traits : NumericTraits or None
        Numeric traits for the events. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::pair_one_between`
        The underlying C++ factory function.
    """

    _factory = "pair_one_between"


@final
class ProcessInBatches(_RelayNode):
    """Processor that buffers a single event type into fixed-size batches and re-emits it.

    Collects every ``batch_size`` events of ``event_type`` into a bucket and
    then re-emits them individually downstream. This is useful for decoupling
    upstream and downstream processing, but introduces latency.

    Parameters
    ----------
    event_type : EventType
        The event type to buffer. The input event set must consist only of
        this type.
    batch_size : int or Param[int]
        Number of events to collect in each batch.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: buffered and re-emitted unchanged.
    - All other event types: rejected at graph build time.
    - End of input: emit any buffered events, then pass through.

    See Also
    --------
    :cpp:`tcspc::process_in_batches`
        The underlying C++ factory function.
    """

    def __init__(
        self, event_type: EventType, batch_size: int | Param[int]
    ) -> None:
        self._event_type = event_type
        self._batch_size = batch_size

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._batch_size, Param):
            return ((self._batch_size, _size_type),)
        return ()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (self._event_type,), self.__class__.__name__
        )
        return (self._event_type,)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        batch_size = gencontext.size_t_expression(self._batch_size)
        return _CppExpression(
            f"""\
            tcspc::process_in_batches<{self._event_type._cpp_type_name()}>(
                tcspc::arg::batch_size<std::size_t>{{{batch_size}}},
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
class RetimePeriodicSequences(_RelayNode):
    """Processor that normalizes the abstime of periodic sequence model events.

    Adjusts the ``abstime`` and ``delay`` of each `PeriodicSequenceModelEvent`
    so the abstime precedes the modeled tick sequence, without altering the
    sequence. Only `PeriodicSequenceModelEvent` is accepted.

    Parameters
    ----------
    max_time_shift : int or Param[int]
        Maximum allowed abstime shift (non-negative); exceeding it is an error.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::retime_periodic_sequences`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        max_time_shift: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._max_time_shift = max_time_shift
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._max_time_shift, Param):
            at = _CppTypeName(
                f"{self._numeric_traits._cpp_type_name()}::abstime_type"
            )
            return ((self._max_time_shift, at),)
        return ()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        model = _events.PeriodicSequenceModelEvent(self._numeric_traits)
        _check_events_subset_of(
            input_event_set, (model,), self.__class__.__name__
        )
        return (model,)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        at = f"{nt}::abstime_type"
        value = _cast_int_expr(gencontext, self._max_time_shift, at)
        return _CppExpression(
            f"""\
            tcspc::retime_periodic_sequences<{nt}>(
                tcspc::arg::max_time_shift<{at}>{{{value}}},
                {downstream}
            )"""
        )


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


@final
class ScanHistograms(_RelayNode):
    """Processor that accumulates an array of histograms over a scan.

    Each `BinIncrementClusterEvent` updates the next element of the histogram
    array; a `HistogramArrayProgressEvent` is emitted per cluster, and a
    `HistogramArrayEvent` upon completion of each scan. A round ends on a
    ``reset_event_type``; if concluding events are enabled, a
    `ConcludingHistogramArrayEvent` is emitted on each reset.

    Parameters
    ----------
    num_elements : int or Param[int]
        Number of histograms (elements) in the array.
    num_bins : int or Param[int]
        Number of bins per element.
    max_per_bin : int or Param[int]
        Maximum count per bin before the overflow policy applies.
    reset_event_type : EventType or None
        Event type that resets (starts a new round). ``None`` (the default)
        means no reset event.
    buffer_provider : BucketSource or Param[PyBucketSource] or None
        Source of buckets holding the histogram array. If ``None``, a default
        `RecyclingBucketSource` is used.
    overflow : str
        Overflow behavior: ``"error"`` (default), ``"stop"``, ``"saturate"``,
        or ``"reset"``.
    emit_concluding : bool
        If ``True``, emit a `ConcludingHistogramArrayEvent` on each reset.
        Default ``False``.
    reset_after_scan : bool
        If ``True``, reset after each completed scan. Default ``False``.
    clear_every_scan : bool
        If ``True``, clear the array at the start of each scan. Default
        ``False``.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``bin_index_type`` and ``bin_type``. Defaults
        to ``NumericTraits()``.

    Notes
    -----
    The histogram-carrying output events cannot be sent to a Python sink; insert
    `ExtractBucket` to obtain the histogram array as a NumPy array.

    See Also
    --------
    :cpp:`tcspc::scan_histograms`
        The underlying C++ factory function.
    :py:obj:`ExtractBucket`
        Extract a histogram-array bucket as a NumPy array.
    """

    def __init__(
        self,
        num_elements: int | Param[int],
        num_bins: int | Param[int],
        max_per_bin: int | Param[int],
        reset_event_type: EventType | None = None,
        *,
        buffer_provider: BucketSource | Param[PyBucketSource] | None = None,
        overflow: str = "error",
        emit_concluding: bool = False,
        reset_after_scan: bool = False,
        clear_every_scan: bool = False,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        if overflow not in _OVERFLOW_POLICIES:
            raise ValueError(
                f"overflow must be one of {sorted(_OVERFLOW_POLICIES)}"
            )
        self._num_elements = num_elements
        self._num_bins = num_bins
        self._max_per_bin = max_per_bin
        self._reset_event_type = reset_event_type
        self._overflow = overflow
        self._emit_concluding = emit_concluding
        self._reset_after_scan = reset_after_scan
        self._clear_every_scan = clear_every_scan
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )
        self._bin_element = _events._TraitsMemberEvent(
            self._numeric_traits, "bin_type"
        )
        self._bucket_source = _bucket_source_or_default(
            self._bin_element, buffer_provider
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        nt = self._numeric_traits._cpp_type_name()
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._num_elements, Param):
            params.append((self._num_elements, _size_type))
        if isinstance(self._num_bins, Param):
            params.append((self._num_bins, _size_type))
        if isinstance(self._max_per_bin, Param):
            params.append((self._max_per_bin, _CppTypeName(f"{nt}::bin_type")))
        params.extend(self._bucket_source._parameters())
        return params

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._bucket_source._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        cluster = _events.BinIncrementClusterEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (cluster,))
        out = _with_event_added(
            out, _events.HistogramArrayProgressEvent(self._numeric_traits)
        )
        out = _with_event_added(
            out, _events.HistogramArrayEvent(self._numeric_traits)
        )
        if self._emit_concluding:
            out = _with_event_added(
                out,
                _events.ConcludingHistogramArrayEvent(self._numeric_traits),
            )
        if self._overflow == "saturate":
            out = _with_event_added(out, WarningEvent())
        return out

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        bt = f"{nt}::bin_type"
        policy = _histogram_policy_expression(
            self._overflow,
            emit_concluding=self._emit_concluding,
            reset_after_scan=self._reset_after_scan,
            clear_every_scan=self._clear_every_scan,
        )
        reset = (
            self._reset_event_type._cpp_type_name()
            if self._reset_event_type is not None
            else "tcspc::never_event"
        )
        num_elements = gencontext.size_t_expression(self._num_elements)
        num_bins = gencontext.size_t_expression(self._num_bins)
        max_per_bin = _cast_int_expr(gencontext, self._max_per_bin, bt)
        return _CppExpression(
            f"""\
            tcspc::scan_histograms<{policy}, {reset}, {nt}>(
                tcspc::arg::num_elements<std::size_t>{{{num_elements}}},
                tcspc::arg::num_bins<std::size_t>{{{num_bins}}},
                tcspc::arg::max_per_bin<{bt}>{{{max_per_bin}}},
                {self._bucket_source._cpp_expression(gencontext)},
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
class SelectNone(_RelayNode):
    """Processor that discards every event.

    Notes
    -----
    Events handled:

    - All event types: discarded.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::select_none`
        The underlying C++ factory function.
    :py:obj:`Select`
        Pass only the listed event types.
    """

    def __init__(self) -> None:
        pass

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(f"tcspc::select_none({downstream})")


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


class _TimeCorrelate(_RelayNode):
    """Common base for the time-correlation relay nodes."""

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        pair = _events.DetectionPairEvent(self._numeric_traits)
        tcd = _events.TimeCorrelatedDetectionEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (pair,))
        return _with_event_added(out, tcd)


@final
class TimeCorrelateAtFraction(_TimeCorrelate):
    """Processor that time-correlates detection pairs at a fractional position.

    The emitted ``abstime`` is interpolated between the start and stop
    detections at the given ``fraction``; the ``channel`` is taken from the
    stop detection by default, or the start detection if ``use_start_channel``
    is true.

    Parameters
    ----------
    fraction : float or Param[float]
        Position between the start (0.0) and stop (1.0) detections.
    use_start_channel : bool
        If ``True``, use the start detection's channel. Default ``False``.
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted events. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::time_correlate_at_fraction`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        fraction: float | Param[float],
        use_start_channel: bool = False,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        super().__init__(numeric_traits)
        self._fraction = fraction
        self._use_start_channel = use_start_channel

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._fraction, Param):
            return ((self._fraction, _CppTypeName("double")),)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        usc = "true" if self._use_start_channel else "false"
        frac = _double_expr(gencontext, self._fraction)
        return _CppExpression(
            f"""\
            tcspc::time_correlate_at_fraction<{self._numeric_traits._cpp_type_name()}, {usc}>(
                tcspc::arg::fraction<double>{{{frac}}},
                {downstream}
            )"""
        )


@final
class TimeCorrelateAtMidpoint(_TimeCorrelate):
    """Processor that time-correlates detection pairs using the midpoint time.

    The emitted ``abstime`` is the midpoint between the start and stop
    detections; the ``channel`` is taken from the stop detection by default,
    or the start detection if ``use_start_channel`` is true.

    Parameters
    ----------
    use_start_channel : bool
        If ``True``, use the start detection's channel. Default ``False``.
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted events. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::time_correlate_at_midpoint`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        use_start_channel: bool = False,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        super().__init__(numeric_traits)
        self._use_start_channel = use_start_channel

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        usc = "true" if self._use_start_channel else "false"
        return _CppExpression(
            f"""\
            tcspc::time_correlate_at_midpoint<{self._numeric_traits._cpp_type_name()}, {usc}>(
                {downstream}
            )"""
        )


@final
class TimeCorrelateAtStart(_TimeCorrelate):
    """Processor that time-correlates detection pairs using the start time and channel.

    Converts each `DetectionPairEvent` into a `TimeCorrelatedDetectionEvent`
    whose ``abstime`` and ``channel`` are taken from the start (first)
    detection and whose ``difftime`` is the interval to the stop detection.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted events. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `DetectionPairEvent`: emit a `TimeCorrelatedDetectionEvent`.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::time_correlate_at_start`
        The underlying C++ factory function.
    """

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::time_correlate_at_start<{self._numeric_traits._cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class TimeCorrelateAtStop(_TimeCorrelate):
    """Processor that time-correlates detection pairs using the stop time and channel.

    Like `TimeCorrelateAtStart`, but the emitted ``abstime`` and ``channel``
    are taken from the stop (second) detection.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted events. Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::time_correlate_at_stop`
        The underlying C++ factory function.
    """

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::time_correlate_at_stop<{self._numeric_traits._cpp_type_name()}>(
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
class UnbatchBinIncrementClusters(_RelayNode):
    """Processor that recovers bin increment clusters from encoded batches.

    The inverse of `BatchBinIncrementClusters`. Each `BucketEvent` of encoded
    bin indices is decoded into individual `BinIncrementClusterEvent`\\ s.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``bin_index_type``. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `BucketEvent` / `ConstBucketEvent` of bin indices: decoded into
      `BinIncrementClusterEvent`\\ s.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::unbatch_bin_increment_clusters`
        The underlying C++ factory function.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )
        self._index_element = _events._TraitsMemberEvent(
            self._numeric_traits, "bin_index_type"
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        mutable = BucketEvent(self._index_element)
        const = _events.ConstBucketEvent(self._index_element)
        cluster = _events.BinIncrementClusterEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (mutable, const))
        return _with_event_added(out, cluster)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::unbatch_bin_increment_clusters<{self._numeric_traits._cpp_type_name()}>(
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
