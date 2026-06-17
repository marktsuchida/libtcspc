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
    _int64_type,
)
from .._events import EventType
from .._node import _RelayNode, _TypePreservingRelayNode
from .._numeric_traits import NumericTraits
from .._param import Param
from ._common import (
    _double_expr,
    _make_type_list,
    _remove_events_from_set,
    _with_event_added,
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

    Notes
    -----
    Events handled:

    - `DetectionPairEvent`: emit a `TimeCorrelatedDetectionEvent`.
    - All other event types: pass through unchanged.
    - End of input: pass through.

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

    Notes
    -----
    Events handled:

    - `DetectionPairEvent`: emit a `TimeCorrelatedDetectionEvent`.
    - All other event types: pass through unchanged.
    - End of input: pass through.

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

    Notes
    -----
    Events handled:

    - `DetectionPairEvent`: emit a `TimeCorrelatedDetectionEvent`.
    - All other event types: pass through unchanged.
    - End of input: pass through.

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
