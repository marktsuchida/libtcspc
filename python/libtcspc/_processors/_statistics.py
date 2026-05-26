# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence
from typing import final

from typing_extensions import override

from .. import _access
from .._access import AccessTag, _AccessSpec
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
)
from .._events import EventType
from .._node import _TypePreservingRelayNode
from .._numeric_traits import NumericTraits


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
    ) -> Sequence[tuple[AccessTag, _AccessSpec]]:
        return ((self._access_tag, _access._CountAccessSpec()),)

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
class RecordAbstimeRange(_TypePreservingRelayNode):
    """Processor that records the range of ``abstime`` observed.

    The minimum and maximum ``abstime`` of all abstime-stamped events seen
    so far can be retrieved at any time during execution via the
    `RecordAbstimeRangeAccess` retrieved from the `ExecutionContext` using
    ``access_tag``.

    Parameters
    ----------
    access_tag : AccessTag
        Tag used to retrieve a `RecordAbstimeRangeAccess` from the
        `ExecutionContext` at runtime.
    numeric_traits : NumericTraits or None
        The numeric traits whose ``abstime_type`` must match the ``abstime``
        field type of the abstime-stamped events passing through. ``None``
        (the default) uses `NumericTraits` defaults.

    Notes
    -----
    Events handled:

    - Events with an ``abstime`` field: update the running minimum and
      maximum, then pass through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::record_abstime_range`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        access_tag: AccessTag,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._access_tag = access_tag
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _accesses(
        self,
    ) -> Sequence[tuple[AccessTag, _AccessSpec]]:
        return (
            (
                self._access_tag,
                _access._RecordAbstimeRangeAccessSpec(self._numeric_traits),
            ),
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        access_type = _CppTypeName(
            f"tcspc::record_abstime_range_access<{nt}::abstime_type>"
        )
        return _CppExpression(
            f"""\
            tcspc::record_abstime_range<{nt}>(
                {gencontext.tracker_expression(access_type, self._access_tag)},
                {downstream}
            )"""
        )
