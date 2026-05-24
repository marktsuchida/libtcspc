# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Collection, Mapping, Sequence
from typing import Any, final

from typing_extensions import override

from .. import _cpp_utils
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
)
from .._events import EventType
from .._node import Node
from .._param import Param
from .._routers import Router
from ._common import (
    _check_events_subset_of,
    _make_type_list,
)


@final
class Broadcast(Node):
    """Broadcasts every event to several downstream processors.

    Each event received is forwarded to every connected downstream. Likewise,
    end-of-input is propagated to every downstream.

    Parameters
    ----------
    *event_types : EventType
        The event types to broadcast. Every output must be connected to a
        downstream that handles all of these types.
    outputs : int or Sequence[str]
        The output ports. An integer ``N`` creates ``N`` ports named
        ``"output-0"`` through ``"output-(N-1)"``. A sequence of names creates
        ports with those exact names. Must specify at least one output.

    Notes
    -----
    Events handled:

    - Events matching one of ``event_types``: broadcast to every downstream.
    - Events not in ``event_types``: rejected at graph build time.
    - End of input: broadcast to every downstream.

    See Also
    --------
    :cpp:`tcspc::broadcast`
        The underlying C++ factory function.
    """

    def __init__(
        self, *event_types: EventType, outputs: int | Sequence[str]
    ) -> None:
        if isinstance(outputs, int):
            if outputs < 1:
                raise ValueError("Broadcast requires at least one output")
            output_names: tuple[str, ...] = tuple(
                f"output-{i}" for i in range(outputs)
            )
        else:
            output_names = tuple(outputs)
            if len(output_names) < 1:
                raise ValueError("Broadcast requires at least one output")
            if len(set(output_names)) != len(output_names):
                raise ValueError("Broadcast output names must be unique")
        super().__init__(output=output_names)
        self._event_types = tuple(event_types)

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 1:
            raise ValueError(
                f"wrong number of inputs (1 expected, {len(input_event_sets)} found)"
            )
        _check_events_subset_of(
            input_event_sets[0], self._event_types, self.__class__.__name__
        )
        return tuple(self._event_types for _ in self.outputs())

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        n = len(self.outputs())
        if len(downstreams) != n:
            raise ValueError(
                f"expected {n} downstream(s); found {len(downstreams)}"
            )
        event_list = _make_type_list(self._event_types)
        args = ", ".join(downstreams)
        return _CppExpression(f"tcspc::broadcast<{event_list}>({args})")


@final
class Route(Node):
    """Routes events to one of several downstreams via a `Router`, broadcasting the rest.

    Each *routed* event is delivered to the single downstream selected by the
    router (or discarded if the router selects no output). Each *broadcast*
    event is delivered to every downstream. End-of-input is propagated to every
    downstream.

    Parameters
    ----------
    *routed_event_types : EventType
        The event types to route. The router selects which output each such
        event is sent to.
    broadcast_event_types : Sequence[EventType], keyword-only
        The event types to broadcast to every downstream. Must not overlap with
        the routed event types. Default: empty.
    router : Router, keyword-only
        The router that maps each routed event to an output-port index.
    outputs : int or Sequence[str], keyword-only
        The output ports. An integer ``N`` creates ``N`` ports named
        ``"output-0"`` through ``"output-(N-1)"``. A sequence of names creates
        ports with those exact names. Must specify at least one output.

    Notes
    -----
    Events handled:

    - Events matching one of ``routed_event_types``: routed to one downstream.
    - Events matching one of ``broadcast_event_types``: broadcast to every
      downstream.
    - Other events: rejected at graph build time.
    - End of input: broadcast to every downstream.

    See Also
    --------
    :cpp:`tcspc::route`
        The underlying C++ factory function.
    :py:obj:`Broadcast`
        Sends every event to every downstream.
    """

    def __init__(
        self,
        *routed_event_types: EventType,
        broadcast_event_types: Sequence[EventType] = (),
        router: Router,
        outputs: int | Sequence[str],
    ) -> None:
        if isinstance(outputs, int):
            if outputs < 1:
                raise ValueError("Route requires at least one output")
            output_names: tuple[str, ...] = tuple(
                f"output-{i}" for i in range(outputs)
            )
        else:
            output_names = tuple(outputs)
            if len(output_names) < 1:
                raise ValueError("Route requires at least one output")
            if len(set(output_names)) != len(output_names):
                raise ValueError("Route output names must be unique")
        super().__init__(output=output_names)
        self._routed = tuple(routed_event_types)
        self._broadcast = tuple(broadcast_event_types)
        self._router = router

        for b in self._broadcast:
            if _cpp_utils._contains_type(
                (r._cpp_type_name() for r in self._routed),
                b._cpp_type_name(),
            ):
                raise ValueError(
                    f"event type {b} must not be both routed and broadcast"
                )

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 1:
            raise ValueError(
                f"wrong number of inputs (1 expected, {len(input_event_sets)} found)"
            )
        allowed = self._routed + self._broadcast
        _check_events_subset_of(
            input_event_sets[0], allowed, self.__class__.__name__
        )
        return tuple(allowed for _ in self.outputs())

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._router._parameters()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._router._param_encoders()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        n = len(self.outputs())
        if len(downstreams) != n:
            raise ValueError(
                f"expected {n} downstream(s); found {len(downstreams)}"
            )
        routed = _make_type_list(self._routed)
        broadcast = _make_type_list(self._broadcast)
        router_expr = self._router._cpp_expression(gencontext)
        args = ", ".join([router_expr, *downstreams])
        return _CppExpression(f"tcspc::route<{routed}, {broadcast}>({args})")
