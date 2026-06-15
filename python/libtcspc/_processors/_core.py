# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence
from typing import final

from typing_extensions import override

from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
)
from .._events import EventInstance, EventType
from .._node import Node, _RelayNode
from ._common import (
    _check_events_subset_of,
    _with_event_added,
)


@final
class SinkAll(Node):
    """Sink that discards every event it receives.

    Notes
    -----
    Events handled:

    - All event types: ignored.
    - End of input: ignored.

    See Also
    --------
    :cpp:`tcspc::sink_all`
        The underlying C++ factory function.
    """

    def __init__(self) -> None:
        super().__init__(output=())

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        return ()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        return _CppExpression("tcspc::sink_all()")


@final
class SourceNothing(_RelayNode):
    """Source processor that emits no events.

    Notes
    -----
    Events handled:

    - This processor has no input events; it is a source.
    - Emits no events.
    - End of input: pass through (flushes the downstream once).

    See Also
    --------
    :cpp:`tcspc::source_nothing`
        The underlying C++ factory function.
    """

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(input_event_set, (), self.__class__.__name__)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(f"tcspc::source_nothing({downstream})")


@final
class Prepend(_RelayNode):
    """Pass-through processor that inserts an event at the start of the stream.

    All events are passed through unchanged. Before the first event is passed
    through, the given ``event`` is emitted once.

    Parameters
    ----------
    event : EventInstance
        The concrete event value to emit before the first passed-through
        event. Construct it via :py:meth:`EventType.value`, for example
        ``DetectionEvent().value(abstime=0, channel=0)``.

    Notes
    -----
    Events handled:

    - All event types: pass through. Before the first one, ``event`` is
      emitted.
    - End of input: pass through.

    Only an `EventInstance` (a concrete value) may be inserted; runtime-supplied
    event values (`Param`) are not yet supported.

    See Also
    --------
    :cpp:`tcspc::prepend`
        The underlying C++ factory function.
    :py:obj:`Append`
        Insert an event at the end of the stream instead.
    """

    def __init__(self, event: EventInstance) -> None:
        # Eagerly reject events that cannot be embedded in compiled code
        # (e.g. bucket-carrying); discard the result -- codegen must re-run
        # _cpp_expression() inside the reference-collecting contexts.
        event._cpp_expression()
        self._event = event

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _with_event_added(input_event_set, self._event._event_type)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"tcspc::prepend({self._event._cpp_expression()}, {downstream})"
        )


@final
class Append(_RelayNode):
    """Pass-through processor that inserts an event at the end of the stream.

    All events are passed through unchanged. Upon flush, the given ``event`` is
    emitted before the flush is propagated.

    Parameters
    ----------
    event : EventInstance
        The concrete event value to emit at flush. Construct it via
        :py:meth:`EventType.value`, for example
        ``DetectionEvent().value(abstime=0, channel=0)``.

    Notes
    -----
    Events handled:

    - All event types: pass through.
    - End of input: emit ``event``, then pass through the flush.

    The event is only appended on a normal flush; if processing is ended by a
    *downstream* processor raising end-of-processing, this processor has no
    effect.

    Only an `EventInstance` (a concrete value) may be inserted; runtime-supplied
    event values (`Param`) are not yet supported.

    See Also
    --------
    :cpp:`tcspc::append`
        The underlying C++ factory function.
    :py:obj:`Prepend`
        Insert an event at the start of the stream instead.
    """

    def __init__(self, event: EventInstance) -> None:
        # Eagerly reject events that cannot be embedded in compiled code
        # (e.g. bucket-carrying); discard the result -- codegen must re-run
        # _cpp_expression() inside the reference-collecting contexts.
        event._cpp_expression()
        self._event = event

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _with_event_added(input_event_set, self._event._event_type)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"tcspc::append({self._event._cpp_expression()}, {downstream})"
        )
