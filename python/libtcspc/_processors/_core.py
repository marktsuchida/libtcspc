# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Collection, Mapping, Sequence
from typing import Any, final

from typing_extensions import override

from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
)
from .._events import EventInstance, EventType
from .._node import Node, _RelayNode
from .._param import Param
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


def _resolve_inserted_event(
    event: EventInstance | Param[EventInstance],
    event_type: EventType | None,
) -> tuple[EventInstance | Param[EventInstance], EventType]:
    # Validate the (event, event_type) pair for Prepend/Append and return the
    # event together with the resolved event type.
    if isinstance(event, Param):
        if event_type is None:
            raise ValueError("event_type is required when event is a Param")
        if not event_type._supports_value():
            raise TypeError(
                f"event type {type(event_type).__name__} "
                f"({event_type._cpp_type_name()}) cannot be inserted as a "
                "Param because its value cannot be constructed from Python "
                "(it does not define convertible fields)"
            )
        if event_type._cpp_wrapper_struct_def() is not None:
            raise TypeError(
                f"array event values ({type(event_type).__name__}) are not "
                "yet supported as a Param"
            )
        default = event.default_value
        if default is not None and (
            not isinstance(default, EventInstance)
            or default._event_type != event_type
        ):
            raise ValueError(
                "default_value of the event Param must be an EventInstance "
                "of the given event_type"
            )
        return event, event_type
    if event_type is not None and event_type != event._event_type:
        raise ValueError("event_type does not match the type of the event")
    # Eagerly reject events that cannot be embedded in compiled code
    # (e.g. bucket-carrying); discard the result -- codegen must re-run
    # _cpp_expression() inside the reference-collecting contexts.
    event._cpp_expression()
    return event, event._event_type


def _inserted_event_encoder(
    event: Param[EventInstance], event_type: EventType
) -> Callable[[Any], Any]:
    def check(value: Any) -> Any:
        if (
            not isinstance(value, EventInstance)
            or value._event_type != event_type
        ):
            raise TypeError(
                f"argument for parameter {event.name!r} must be an "
                f"EventInstance of type {type(event_type).__name__}"
            )
        return value

    return check


@final
class Prepend(_RelayNode):
    """Pass-through processor that inserts an event at the start of the stream.

    All events are passed through unchanged. Before the first event is passed
    through, the given ``event`` is emitted once.

    Parameters
    ----------
    event : EventInstance or Param[EventInstance]
        The event value to emit before the first passed-through event. A
        concrete `EventInstance` (construct it via :py:meth:`EventType.value`,
        for example ``DetectionEvent().value(abstime=0, channel=0)``) is baked
        into the generated code. A `Param` binds the event value at execution
        time; ``event_type`` must then be given.
    event_type : EventType or None, keyword-only
        The type of the inserted event. Required if and only if ``event`` is a
        `Param`. For a concrete `EventInstance` it is inferred and, if given,
        must equal the event's type.

    Notes
    -----
    Events handled:

    - All event types: pass through. Before the first one, ``event`` is
      emitted.
    - End of input: pass through.

    When ``event`` is a `Param`, the event type must have plain (scalar,
    raw-bytes, or bucket) fields; array events are not yet supported as a
    `Param`.

    See Also
    --------
    :cpp:`tcspc::prepend`
        The underlying C++ factory function.
    :py:obj:`Append`
        Insert an event at the end of the stream instead.
    """

    def __init__(
        self,
        event: EventInstance | Param[EventInstance],
        *,
        event_type: EventType | None = None,
    ) -> None:
        self._event, self._event_type = _resolve_inserted_event(
            event, event_type
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _with_event_added(input_event_set, self._event_type)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._event, Param):
            return ((self._event, self._event_type._cpp_type_name()),)
        return ()

    @override
    def _value_event_types(self) -> Sequence[EventType]:
        return (self._event_type,) if isinstance(self._event, Param) else ()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        if isinstance(self._event, Param):
            return {
                self._event.name: _inserted_event_encoder(
                    self._event, self._event_type
                )
            }
        return {}

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        if isinstance(self._event, Param):
            self._event_type._cpp_type_name()  # Registers custom traits.
            arg = (
                f"{gencontext.params_varname}.{self._event._cpp_identifier()}"
            )
        else:
            arg = self._event._cpp_expression()
        return _CppExpression(f"tcspc::prepend({arg}, {downstream})")


@final
class Append(_RelayNode):
    """Pass-through processor that inserts an event at the end of the stream.

    All events are passed through unchanged. Upon flush, the given ``event`` is
    emitted before the flush is propagated.

    Parameters
    ----------
    event : EventInstance or Param[EventInstance]
        The event value to emit at flush. A concrete `EventInstance`
        (construct it via :py:meth:`EventType.value`, for example
        ``DetectionEvent().value(abstime=0, channel=0)``) is baked into the
        generated code. A `Param` binds the event value at execution time;
        ``event_type`` must then be given.
    event_type : EventType or None, keyword-only
        The type of the inserted event. Required if and only if ``event`` is a
        `Param`. For a concrete `EventInstance` it is inferred and, if given,
        must equal the event's type.

    Notes
    -----
    Events handled:

    - All event types: pass through.
    - End of input: emit ``event``, then pass through the flush.

    The event is only appended on a normal flush; if processing is ended by a
    *downstream* processor raising end-of-processing, this processor has no
    effect.

    When ``event`` is a `Param`, the event type must have plain (scalar,
    raw-bytes, or bucket) fields; array events are not yet supported as a
    `Param`.

    See Also
    --------
    :cpp:`tcspc::append`
        The underlying C++ factory function.
    :py:obj:`Prepend`
        Insert an event at the start of the stream instead.
    """

    def __init__(
        self,
        event: EventInstance | Param[EventInstance],
        *,
        event_type: EventType | None = None,
    ) -> None:
        self._event, self._event_type = _resolve_inserted_event(
            event, event_type
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _with_event_added(input_event_set, self._event_type)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._event, Param):
            return ((self._event, self._event_type._cpp_type_name()),)
        return ()

    @override
    def _value_event_types(self) -> Sequence[EventType]:
        return (self._event_type,) if isinstance(self._event, Param) else ()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        if isinstance(self._event, Param):
            return {
                self._event.name: _inserted_event_encoder(
                    self._event, self._event_type
                )
            }
        return {}

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        if isinstance(self._event, Param):
            self._event_type._cpp_type_name()  # Registers custom traits.
            arg = (
                f"{gencontext.params_varname}.{self._event._cpp_identifier()}"
            )
        else:
            arg = self._event._cpp_expression()
        return _CppExpression(f"tcspc::append({arg}, {downstream})")
