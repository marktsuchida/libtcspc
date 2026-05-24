# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Collection, Mapping, Sequence
from typing import Any, final

from typing_extensions import override

from .. import _events
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
from .._events import BucketEvent, EventType
from .._node import _RelayNode
from .._numeric_traits import NumericTraits
from .._param import Param
from ._common import (
    _bucket_source_or_default,
    _check_events_subset_of,
    _remove_events_from_set,
    _with_event_added,
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
