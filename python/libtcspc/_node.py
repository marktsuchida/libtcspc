# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence
from typing import final

from typing_extensions import override

from ._access import Accessible
from ._codegen import CodeGenerationContext
from ._cpp_utils import CppExpression
from ._events import EventType
from ._param import Parameterized


class Node(Accessible, Parameterized):
    """
    Base class for a processing graph node.

    All nodes are pure data objects that can be pickled or deep-copied. They
    are meant to be fully configured upon creation and not subsequently
    modified.

    Nodes have named input and output ports. Many nodes representing processors
    have a single input port named ``"input"`` and a single output port named
    ``"output"``. Input and output ports represent the possible connections
    that can be made between nodes in a graph. Every node must have at least
    one input port, but may or may not have output ports.

    In order to support checking for event type compatibility between nodes, a
    node is able to compute the set of events emitted on its output ports given
    the set of events received on its input ports. Also, they have the
    capability to generate C++ code fragments to instantiate executable code.
    Methods for these capabilities are usually not directly called from user
    code.
    """

    _inputs: tuple[str, ...] = ("input",)  # Default: single input
    _outputs: tuple[str, ...] = ("output",)  # Default: single output

    def __init__(
        self,
        *,
        input: Sequence[str] | None = None,
        output: Sequence[str] | None = None,
    ) -> None:
        """
        Initialize with the given ports.

        A subclass's `__init__` must call this function if the node is not a
        single-input, single-output node.

        Parameters
        ----------
        input
            The names of the input ports of the node. If not given, the node
            has a single input port named "input".
        output
            The names of the output ports of the node. If not given, the node
            has a single output port named "output".
        """
        if input is not None:
            self._inputs = tuple(input)
        if output is not None:
            self._outputs = tuple(output)

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}(inputs={repr(self.inputs())}, outputs={repr(self.outputs())})>"

    @final
    def inputs(self) -> tuple[str, ...]:
        """
        Returns the names of the node's input ports.
        """
        return self._inputs

    @final
    def outputs(self) -> tuple[str, ...]:
        """
        Returns the names of the node's output ports.
        """
        return self._outputs

    def map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        """
        Given the sets of events received on each input port, return the sets
        of events emitted on each output port.

        This method must be overridden by concrete node classes.

        Parameters
        ----------
        input_event_sets
            For each input port, the set of events that the upstream node
            emits.

        Returns
        -------
        tuple[tuple[EventType, ...], ...]
            For each output port, the events that must be handled by a
            connected downstream node.

        Raises
        ------
        ValueError
            If `input_event_sets` contains events that are incompatible with
            this node.
        """
        raise NotImplementedError()

    def cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstreams: Sequence[CppExpression],
    ) -> CppExpression:
        """
        Returns C++ code for this node and its downstreams.

        This method must be overridden by concrete node classes.

        Parameters
        ----------
        gencontext
            Contextual information required for generating code.
        downstreams
            For each output port, the C++ code representing an rvalue reference
            to the downstream.

        Returns
        -------
        CppExpression
            C++ code for this node.
        """
        raise NotImplementedError()


class RelayNode(Node):
    """
    A node representing a single-upstream, single-downstream processor.

    Most nodes are relay nodes.

    This base class simplifies the implementation of relay nodes: subclasses
    must override the simplified methods `relay_map_event_set()` and
    `relay_cpp_expression()`, instead of the `Node` methods `map_event_sets()`
    and `cpp_expression()`. Implementations of the latter methods is provided
    by `RelayNode`.
    """

    @override
    def __init__(self) -> None:
        """
        Initialize.
        """
        pass

    @override
    @final
    def map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        """
        Maps events sets based on `relay_map_event_set()`.
        """
        n_upstreams = len(input_event_sets)
        if n_upstreams != 1:
            raise ValueError(
                f"expected a single upstream; found {n_upstreams}"
            )
        return (self.relay_map_event_set(input_event_sets[0]),)

    def relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        """
        Given the set of events received on the input port, return the set of
        events emitted on the output port.

        This is analogous to `map_event_sets` but for the single input and
        output ports of the relay node. Concrete subclasses must override this
        method.

        Parameters
        ----------
        input_event_set
            The events that the upstream node emits.

        Returns
        -------
        tuple[EventType, ...]
            The events that must be handled by a connected downstream node.

        Raises
        ------
        ValueError
            If `input_event_set` contains events that are incompatible with
            this node.
        """
        raise NotImplementedError()

    @override
    @final
    def cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstreams: Sequence[CppExpression],
    ) -> CppExpression:
        """
        Generates C++ code based on `relay_cpp_expression()`.
        """
        n_downstreams = len(downstreams)
        if n_downstreams != 1:
            raise ValueError(
                f"expected a single downstream; found {n_downstreams}"
            )
        return self.relay_cpp_expression(gencontext, downstreams[0])

    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        """
        Returns C++ code for this node and its downstream.

        This is analogous to `cpp_expression` but for the single downstream of
        the relay node. Concrete subclasses must override this method.

        Parameters
        ----------
        gencontext
            Contextual information required for generating code.
        downstream
            The C++ code representing an rvalue reference ot the downstream.

        Returns
        -------
        str
            C++ code for this node.
        """
        raise NotImplementedError()


class TypePreservingRelayNode(RelayNode):
    """
    A relay node whose output event set matches its input event set.

    Subclasses need only override `relay_cpp_expression`.
    """

    @override
    def relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return tuple(input_event_set)
