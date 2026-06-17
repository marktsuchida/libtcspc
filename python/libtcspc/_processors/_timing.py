# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Collection, Mapping, Sequence
from typing import Any, final

from typing_extensions import override

from .. import _cpp_utils
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _uint64_type,
)
from .._events import EventType
from .._matchers import Matcher
from .._node import _RelayNode
from .._param import Param
from .._timing_generators import TimingGenerator
from ._common import (
    _with_event_added,
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
        if not _cpp_utils._contains_event_type(out, self._fire_event_type):
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
        if not _cpp_utils._contains_event_type(out, self._fire_event_type):
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

    Notes
    -----
    Events handled:

    - ``event_type`` matched by the matcher: emit an ``out_event_type``
      constructed from the matched event's ``abstime``.
    - All input events: pass through unchanged (matched or not).
    - End of input: pass through.

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

    Notes
    -----
    Events handled:

    - ``event_type`` matched by the matcher: emit an ``out_event_type``
      constructed from the matched event's ``abstime``; the matched event is
      consumed (not passed through).
    - All other input events (including unmatched ``event_type``): pass
      through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::match_and_consume`
        The underlying C++ factory function.
    :py:obj:`Match`
        Like `MatchAndConsume`, but matched events are also passed through.
    """

    _factory = "match_and_consume"
