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
from .._events import EventType
from .._node import Node
from ._common import (
    _check_events_subset_of,
)


@final
class SinkOnly(Node):
    """Sink that accepts and discards a fixed set of event types.

    Most useful for testing, or for asserting at graph-build time that an
    upstream subgraph only produces a known set of event types — any
    other event type connected to this sink is rejected at graph build
    time.

    Parameters
    ----------
    *event_types : EventType
        The only event types the sink will accept.

    Notes
    -----
    Events handled:

    - Events matching one of ``event_types``: ignored.
    - Events not in ``event_types``: rejected at graph build time.
    - End of input: ignored.

    See Also
    --------
    :cpp:`tcspc::sink_only`
        The underlying C++ factory function.
    """

    def __init__(self, *event_types: EventType) -> None:
        super().__init__(output=())
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
        return ()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        evts = ", ".join(t._cpp_type_name() for t in self._event_types)
        return _CppExpression(f"tcspc::sink_only<{evts}>()")
