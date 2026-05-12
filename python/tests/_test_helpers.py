# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence

from libtcspc._codegen import CodeGenerationContext
from libtcspc._cpp_utils import CppExpression, CppTypeName
from libtcspc._events import EventType
from libtcspc._node import Node, _RelayNode
from typing_extensions import override


class _NamedEvent(EventType):
    def __init__(self, cpp_type: CppTypeName) -> None:
        self._cpp_type = cpp_type

    @override
    def _cpp_type_name(self) -> CppTypeName:
        return self._cpp_type


class _TestNode(Node):
    """Concrete `Node` for tests; the abstract methods are stubs intended to
    be replaced by `mocker.MagicMock` on the instance."""

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        raise NotImplementedError

    @override
    def _cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstreams: Sequence[CppExpression],
    ) -> CppExpression:
        raise NotImplementedError


class _TestRelayNode(_RelayNode):
    """Concrete `_RelayNode` for tests; the abstract methods are stubs intended
    to be replaced by `mocker.MagicMock` on the instance."""

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        raise NotImplementedError

    @override
    def _relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        raise NotImplementedError
