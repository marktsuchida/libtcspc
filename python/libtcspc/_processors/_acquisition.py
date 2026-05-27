# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence
from typing import final

from typing_extensions import override

from .. import _access, _events
from .._access import AccessTag, _AccessorSpec
from .._acquisition_readers import AcquisitionReader, PyAcquisitionReader
from .._bucket_sources import (
    BucketSource,
    PyBucketSource,
)
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _size_type,
)
from .._events import EventType
from .._node import Node, _RelayNode
from .._param import Param
from ._common import (
    _bucket_source_or_default,
    _check_events_subset_of,
    _remove_events_from_set,
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
    acquisition is halted via the `AcquireAccessor` retrieved from the
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
        Tag used to retrieve an `AcquireAccessor` (which provides ``halt()``)
        from the `ExecutionContext` at runtime.

    Notes
    -----
    Events handled:

    - This processor has no input events; it is a source.
    - Emits `BucketEvent` of ``event_type`` whenever a non-empty read
      completes.
    - End of input is initiated when the reader returns ``None``,
      whereupon the downstream is flushed. If halted via `AcquireAccessor`
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
    :py:obj:`AcquireAccessor`
        Runtime accessor providing ``halt()``.
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
    def _accesses(self) -> Sequence[tuple[AccessTag, _AccessorSpec]]:
        return ((self._access_tag, _access._AcquireAccessorSpec()),)

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
                {gencontext.tracker_expression(_CppTypeName("tcspc::acquire_accessor"), self._access_tag)},
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
    the `AcquireAccessor` retrieved from the `ExecutionContext` using
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
        Tag used to retrieve an `AcquireAccessor` (which provides ``halt()``) from
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
      first). If halted via `AcquireAccessor` before end of stream, the
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
    :py:obj:`AcquireAccessor`
        Runtime accessor providing ``halt()``.
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
    def _accesses(self) -> Sequence[tuple[AccessTag, _AccessorSpec]]:
        return ((self._access_tag, _access._AcquireAccessorSpec()),)

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
                {gencontext.tracker_expression(_CppTypeName("tcspc::acquire_accessor"), self._access_tag)},
                {live},
                {batch}
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
