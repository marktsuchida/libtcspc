# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection
from typing import final

from typing_extensions import override

from .. import _cpp_utils
from .._codegen import _CodeGenerationContext
from .._cpp_utils import _CppExpression
from .._events import EventType, VariantEvent
from .._node import _RelayNode
from ._common import _make_type_list


@final
class Multiplex(_RelayNode):
    """Processor that wraps events of several types into a single variant type.

    The listed event types are combined into a single `VariantEvent` carrying
    whichever event arrived. This is typically used to buffer a stream of more
    than one event type through a stage that handles only a single type;
    `Demultiplex` reverses the effect.

    Parameters
    ----------
    *event_types : EventType
        The event types to combine. The order is *significant* and is part of
        the resulting `VariantEvent` type.

    Notes
    -----
    Events handled:

    - Events in ``event_types``: passed through wrapped in a `VariantEvent`
      spanning all of ``event_types``.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::multiplex`
        The underlying C++ factory function.
    :py:obj:`Demultiplex`
        Restore the individual event types.
    :py:obj:`VariantEvent`
        The emitted event type.
    """

    def __init__(self, *event_types: EventType) -> None:
        if not event_types:
            raise ValueError("Multiplex requires at least one event type")
        self._event_types = tuple(event_types)

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        for t in input_event_set:
            if not _cpp_utils._contains_event_type(self._event_types, t):
                raise ValueError(f"input type {t} not accepted by Multiplex")
        return (VariantEvent(*self._event_types),)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::multiplex<
                {_make_type_list(self._event_types)}
            >(
                {downstream}
            )"""
        )


@final
class Demultiplex(_RelayNode):
    """Processor that unwraps a variant type back to individual event types.

    Reverses the effect of `Multiplex`: each incoming `VariantEvent` is
    unwrapped and the carried event is emitted as its individual type.

    The input must consist only of `VariantEvent`(s); non-variant input events
    are rejected. This matches the typical buffering use case, where the events
    have just been carried through a single-type stage as a `VariantEvent`.

    Notes
    -----
    Events handled:

    - `VariantEvent`: unwrapped; the carried event passes through as its
      individual type.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::demultiplex`
        The underlying C++ factory function.
    :py:obj:`Multiplex`
        Combine the individual event types into a `VariantEvent`.
    :py:obj:`VariantEvent`
        The accepted input event type.
    """

    def __init__(self) -> None:
        pass

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        output: list[EventType] = []
        for t in input_event_set:
            if not isinstance(t, VariantEvent):
                raise ValueError(
                    f"input type {t} not accepted by Demultiplex (its input "
                    "must be a VariantEvent, as produced by Multiplex)"
                )
            output.extend(t._event_types)
        return tuple(output)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(f"tcspc::demultiplex({downstream})")
