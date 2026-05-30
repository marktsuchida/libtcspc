# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence
from typing import TYPE_CHECKING, final

from typing_extensions import override

from .._access import AccessTag, _AccessorSpec, _BufferAccessorSpec
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _int64_type,
    _size_type,
)
from .._events import EventType
from .._node import _RelayNode
from .._param import Param
from ._common import (
    _check_events_subset_of,
)

if TYPE_CHECKING:
    from .._graph import _ThreadColoring, _ThreadGroup

_buffer_accessor_type = _CppTypeName("tcspc::buffer_accessor")


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
class Buffer(_RelayNode):
    """Processor that buffers a single event type for emission on another thread.

    A buffer splits the processing graph into a *producer half* (the
    processors upstream of this node) and a *consumer half* (the processors
    downstream). Events of ``event_type`` are enqueued by the producer thread
    (the one driving `ExecutionContext.handle` / `ExecutionContext.flush`) and
    are drained and re-emitted unchanged on a separate *pump thread* that the
    application runs via `BufferAccessor.pump`. The buffer thus decouples
    upstream and downstream processing and lets them run concurrently.

    A heterogeneous event stream can be buffered by composing with `Multiplex`
    and `Demultiplex` so that a single `VariantEvent` type crosses the buffer.

    Parameters
    ----------
    event_type : EventType
        The event type to buffer. The input event set must consist only of
        this type.
    threshold : int or Param[int]
        Number of events to accumulate before they are made available to the
        pump thread.
    access_tag : AccessTag
        Tag used to retrieve a `BufferAccessor` from the `ExecutionContext`
        at runtime; the application uses it to drive the pump thread.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: buffered and re-emitted unchanged on the
      pump thread.
    - All other event types: rejected at graph build time.
    - End of input: drained, then passed through on the pump thread.

    `ExecutionContext.flush` returns once the producer half is flushed; it does
    not wait for the consumer half to drain. See `BufferAccessor` for the
    threading contract.

    See Also
    --------
    :cpp:`tcspc::buffer`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_type: EventType,
        threshold: int | Param[int],
        access_tag: AccessTag,
    ) -> None:
        self._event_type = event_type
        self._threshold = threshold
        self._access_tag = access_tag

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._threshold, Param):
            return ((self._threshold, _size_type),)
        return ()

    @override
    def _accesses(self) -> Sequence[tuple[AccessTag, _AccessorSpec]]:
        return ((self._access_tag, _BufferAccessorSpec()),)

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (self._event_type,), self.__class__.__name__
        )
        return (self._event_type,)

    @override
    def _map_thread_groups(
        self,
        input_groups: Sequence["_ThreadGroup"],
        coloring: "_ThreadColoring",
    ) -> tuple["_ThreadGroup", ...]:
        # Events are re-emitted on the pump thread, a different thread than the
        # producer; mint a fresh group regardless of the input group.
        return (coloring.mint(),)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        threshold = gencontext.size_t_expression(self._threshold)
        tracker = gencontext.tracker_expression(
            _buffer_accessor_type, self._access_tag
        )
        return _CppExpression(
            f"""\
            tcspc::buffer<{self._event_type._cpp_type_name()}>(
                tcspc::arg::threshold<std::size_t>{{{threshold}}},
                {tracker},
                {downstream}
            )"""
        )


@final
class RealTimeBuffer(_RelayNode):
    """Processor that buffers events for emission on another thread, with bounded latency.

    Like `Buffer`, but additionally bounds the time an event may wait in the
    buffer: events are made available to the pump thread when either
    ``threshold`` events have accumulated or the oldest buffered event has been
    held for ``latency_limit`` (whichever comes first). The latency limit is
    measured in wall-clock time (a steady clock), not in event abstime.

    Parameters
    ----------
    event_type : EventType
        The event type to buffer. The input event set must consist only of
        this type.
    threshold : int or Param[int]
        Number of events to accumulate before they are made available to the
        pump thread even if the latency limit has not been reached.
    latency_limit : int or Param[int]
        Maximum time, in **nanoseconds**, that an event may remain buffered
        before it is made available to the pump thread even if fewer than
        ``threshold`` events have accumulated. Must be non-negative and not
        exceed 24 hours.
    access_tag : AccessTag
        Tag used to retrieve a `BufferAccessor` from the `ExecutionContext`
        at runtime; the application uses it to drive the pump thread.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: buffered and re-emitted unchanged on the
      pump thread.
    - All other event types: rejected at graph build time.
    - End of input: drained, then passed through on the pump thread.

    See `Buffer` and `BufferAccessor` for the threading contract.

    See Also
    --------
    :cpp:`tcspc::real_time_buffer`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_type: EventType,
        threshold: int | Param[int],
        latency_limit: int | Param[int],
        access_tag: AccessTag,
    ) -> None:
        if isinstance(latency_limit, int) and latency_limit < 0:
            raise ValueError("latency_limit must be non-negative")
        self._event_type = event_type
        self._threshold = threshold
        self._latency_limit = latency_limit
        self._access_tag = access_tag

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._threshold, Param):
            params.append((self._threshold, _size_type))
        if isinstance(self._latency_limit, Param):
            params.append((self._latency_limit, _int64_type))
        return tuple(params)

    @override
    def _accesses(self) -> Sequence[tuple[AccessTag, _AccessorSpec]]:
        return ((self._access_tag, _BufferAccessorSpec()),)

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (self._event_type,), self.__class__.__name__
        )
        return (self._event_type,)

    @override
    def _map_thread_groups(
        self,
        input_groups: Sequence["_ThreadGroup"],
        coloring: "_ThreadColoring",
    ) -> tuple["_ThreadGroup", ...]:
        # Events are re-emitted on the pump thread, a different thread than the
        # producer; mint a fresh group regardless of the input group.
        return (coloring.mint(),)

    def _latency_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        if isinstance(self._latency_limit, Param):
            ns = _CppExpression(
                f"{gencontext.params_varname}."
                f"{self._latency_limit._cpp_identifier()}"
            )
        else:
            ns = _CppExpression(str(self._latency_limit))
        return _CppExpression(f"std::chrono::nanoseconds{{{ns}}}")

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        threshold = gencontext.size_t_expression(self._threshold)
        latency = self._latency_expression(gencontext)
        tracker = gencontext.tracker_expression(
            _buffer_accessor_type, self._access_tag
        )
        return _CppExpression(
            f"""\
            tcspc::real_time_buffer<{self._event_type._cpp_type_name()}>(
                tcspc::arg::threshold<std::size_t>{{{threshold}}},
                {latency},
                {tracker},
                {downstream}
            )"""
        )
