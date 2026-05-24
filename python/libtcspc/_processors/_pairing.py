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
)
from .._events import EventType
from .._node import _RelayNode
from .._numeric_traits import NumericTraits
from .._param import Param
from ._common import (
    _cast_int_expr,
    _with_event_added,
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
