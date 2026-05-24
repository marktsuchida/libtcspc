# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence
from typing import final

from typing_extensions import override

from .. import _cpp_utils
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
)
from .._events import EventType
from .._node import _RelayNode, _TypePreservingRelayNode
from .._param import Param
from ._common import (
    _make_type_list,
    _remove_events_from_set,
)


@final
class Gate(_TypePreservingRelayNode):
    """Pass-through processor that gates events depending on open/close state.

    Maintains a boolean gate. When an ``open_event_type`` is received the gate
    opens; when a ``close_event_type`` is received it closes. Events of the
    gated types are passed through only while the gate is open; all other
    events (including the open and close events) always pass through.

    Parameters
    ----------
    *gated_event_types : EventType
        Event types to gate.
    open_event_type : EventType, keyword-only
        Event type that opens the gate.
    close_event_type : EventType, keyword-only
        Event type that closes the gate.
    initially_open : bool or Param[bool], keyword-only
        Initial state of the gate. Default ``False``.

    Notes
    -----
    Events handled:

    - Events in ``gated_event_types``: passed through only while open.
    - ``open_event_type`` / ``close_event_type``: open/close the gate and pass
      through.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::gate`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        *gated_event_types: EventType,
        open_event_type: EventType,
        close_event_type: EventType,
        initially_open: bool | Param[bool] = False,
    ) -> None:
        self._gated = tuple(gated_event_types)
        self._open = open_event_type
        self._close = close_event_type
        self._initially_open = initially_open

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._initially_open, Param):
            return ((self._initially_open, _CppTypeName("bool")),)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        gated_list = _make_type_list(self._gated)
        if isinstance(self._initially_open, Param):
            value = f"{gencontext.params_varname}.{self._initially_open._cpp_identifier()}"
        else:
            value = "true" if self._initially_open else "false"
        return _CppExpression(
            f"""\
            tcspc::gate<
                {gated_list},
                {self._open._cpp_type_name()},
                {self._close._cpp_type_name()}
            >(
                tcspc::arg::initially_open{{static_cast<bool>({value})}},
                {downstream}
            )"""
        )


@final
class Select(_RelayNode):
    """Processor that passes only events of the listed types, dropping others.

    Parameters
    ----------
    *event_types : EventType
        The event types to pass through. Events of all other types are
        silently discarded.

    Notes
    -----
    Events handled:

    - Events in ``event_types``: pass through unchanged.
    - All other event types: discarded.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::select`
        The underlying C++ factory function.
    :py:obj:`SelectAll`
        Forward every event unchanged.
    :py:obj:`SelectExcept`
        Discard the listed types, passing others.
    """

    def __init__(self, *event_types: EventType) -> None:
        self._event_types = tuple(event_types)

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return tuple(
            t
            for t in input_event_set
            if _cpp_utils._contains_type(
                (u._cpp_type_name() for u in self._event_types),
                t._cpp_type_name(),
            )
        )

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::select<
                {_make_type_list(self._event_types)}
            >(
                {downstream}
            )"""
        )


@final
class SelectAll(_TypePreservingRelayNode):
    """Pass-through processor that forwards every event unchanged.

    This is a no-op filter. It is occasionally useful as a placeholder
    where the graph structure expects a processor but no transformation
    is needed.

    Notes
    -----
    Events handled:

    - All event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::select_all`
        The underlying C++ factory function.
    """

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(f"tcspc::select_all({downstream})")


@final
class SelectExcept(_RelayNode):
    """Processor that discards events of the listed types, passing others.

    The complement of `Select`.

    Parameters
    ----------
    *event_types : EventType
        The event types to discard. Events of all other types pass
        through unchanged.

    Notes
    -----
    Events handled:

    - Events in ``event_types``: discarded.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::select_except`
        The underlying C++ factory function.
    :py:obj:`Select`
        Pass only the listed types, dropping others.
    :py:obj:`SelectAll`
        Forward every event unchanged.
    """

    def __init__(self, *event_types: EventType) -> None:
        self._event_types = tuple(event_types)

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::select_except<
                {_make_type_list(self._event_types)}
            >(
                {downstream}
            )"""
        )


@final
class SelectNone(_RelayNode):
    """Processor that discards every event.

    Notes
    -----
    Events handled:

    - All event types: discarded.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::select_none`
        The underlying C++ factory function.
    :py:obj:`Select`
        Pass only the listed event types.
    """

    def __init__(self) -> None:
        pass

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(f"tcspc::select_none({downstream})")
