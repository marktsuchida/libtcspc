# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection
from typing import final

from typing_extensions import override

from .. import _cpp_utils
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
)
from .._events import EventType, WarningEvent
from .._node import _RelayNode
from .._numeric_traits import NumericTraits


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
