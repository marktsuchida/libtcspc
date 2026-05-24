# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence
from typing import final

from typing_extensions import override

from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _int64_type,
    _size_type,
)
from .._node import _TypePreservingRelayNode
from .._numeric_traits import NumericTraits
from .._param import Param


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
