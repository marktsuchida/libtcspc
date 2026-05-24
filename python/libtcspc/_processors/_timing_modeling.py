# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence
from typing import final

from typing_extensions import override

from .. import _events
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _size_type,
)
from .._events import EventType
from .._node import _RelayNode
from .._numeric_traits import NumericTraits
from .._param import Param
from ._common import (
    _cast_int_expr,
    _check_events_subset_of,
    _double_expr,
    _remove_events_from_set,
    _with_event_added,
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
