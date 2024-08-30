# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import itertools
from collections.abc import Callable, Collection, Mapping, Sequence
from copy import deepcopy
from dataclasses import dataclass
from textwrap import dedent
from typing import final

import cppyy
from typing_extensions import override

from ._access import Accessible
from ._events import EventType
from ._param import Parameterized

cppyy.include("tuple")
cppyy.include("utility")


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

    def generate_cpp(
        self,
        node_name: str,
        context_varname: str,
        params_varname: str,
        parameters: Mapping[str, str],
        downstreams: Sequence[str],
    ) -> str:
        """
        Returns C++ code for this node and its downstreams.

        This method must be overridden by concrete node classes.

        Parameters
        ----------
        node_name
            The unique name of this node, optionally used for access tracker
            naming.
        context_varname
            The context (C++ variable name).
        params_varname
            Parameterization data (C++ variable name)
        parameters
            The C++ expression for each named parameter.
        downstreams
            For each output port, the C++ code representing an rvalue reference
            to the downstream.

        Returns
        -------
        str
            C++ code for this node.
        """
        raise NotImplementedError()


class OneToOneNode(Node):
    """
    A node representing a single-upstream, single-downstream processor.

    A common base class that simplifies implementation of the most common type
    of node. Subclasses override simplified methods (`map_event_set`,
    `generate_cpp_one_to_one`), avoiding boilerplate that deals with the more
    general multi-input, multi-output node interface.
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
        n_upstreams = len(input_event_sets)
        if n_upstreams != 1:
            raise ValueError(
                f"expected a single upstream; found {n_upstreams}"
            )
        return (self.map_event_set(input_event_sets[0]),)

    def map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        """
        Given the set of events received on the input port, return the set of
        events emitted on the output port.

        This is analogous to `map_event_sets` but for the single input and
        output ports. Concrete subclasses must override this method.

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
    def generate_cpp(
        self,
        node_name: str,
        context_varname: str,
        params_varname: str,
        parameters: Mapping[str, str],
        downstreams: Sequence[str],
    ) -> str:
        n_downstreams = len(downstreams)
        if n_downstreams != 1:
            raise ValueError(
                f"expected a single downstream; found {n_downstreams}"
            )
        return self.generate_cpp_one_to_one(
            node_name,
            context_varname,
            params_varname,
            parameters,
            downstreams[0],
        )

    def generate_cpp_one_to_one(
        self,
        node_name: str,
        context_varname: str,
        params_varname: str,
        parameters: Mapping[str, str],
        downstream: str,
    ) -> str:
        """
        Returns C++ code for this node and its downstream.

        This is analogous to `generate_cpp` but for the single downstream.
        Concrete subclasses must override this method.

        Parameters
        ----------
        node_name
            The unique name of this node, optionally used for access tracker
            naming.
        context_varname
            The context (C++ variable name).
        params_varname
            Parameterization data (C++ variable name).
        parameters
            The C++ expression for each named parameter.
        downstream
            The C++ code representing an rvalue reference ot the downstream.

        Returns
        -------
        str
            C++ code for this node.
        """
        raise NotImplementedError()


class OneToOnePassThroughNode(OneToOneNode):
    """
    A one-to-one node whose output event set matches its input event set.

    Subclasses need only override `generate_cpp_one_to_one`.
    """

    @override
    def map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return tuple(input_event_set)


@dataclass
class _Edge:
    # Edge connecting nodes in a Graph. This is a private type because it only
    # has meaning in the internal context of a Graph.
    producer_id: int
    consumer_id: int
    event_set: tuple[EventType, ...]  # Updated when an upstream edge is added


def _update_topological_order(
    tsorted_nodes: list[int],
    input_edges: dict[int, list[_Edge | None]],
    output_edges: dict[int, list[_Edge | None]],
    new_edge: _Edge,
) -> None:
    # - 'nodes' is updated in place. The other arguments are not modified.
    # - 'new_edge' may or may not be in the edge dicts, but 'nodes' must be
    #   topologically sorted when 'new_edge' is ignored.

    # Use Pearce-Kelly (2007) algorithm to update the topological order for the
    # newly added edge. https://doi.org/10.1145/1187436.1210590 or
    # https://whileydave.com/publications/PK07_JEA_preprint.pdf

    iproducer = tsorted_nodes.index(new_edge.producer_id)
    iconsumer = tsorted_nodes.index(new_edge.consumer_id)
    if iproducer < iconsumer:  # Already in order.
        return

    # The affected region (AR) is from iconsumer to iproducer.
    swappable_indices: list[int] = [iconsumer, iproducer]

    # Find nodes reachable from consumer and within the AR.
    forward_node_ids: list[int] = [new_edge.consumer_id]
    for i in range(iconsumer + 1, iproducer):
        node_id = tsorted_nodes[i]
        for edge in input_edges[node_id]:
            if edge is not None and edge.producer_id in forward_node_ids:
                forward_node_ids.append(node_id)
                swappable_indices.append(i)
                break

    # Did/will we introduce a cycle (= producer reachable from consumer)?
    for edge in input_edges[new_edge.producer_id]:
        if edge is not None and edge.producer_id in forward_node_ids:
            raise ValueError("cycle introduced in graph")

    # Find nodes that reach producer and are within the AR.
    backward_node_ids: list[int] = [new_edge.producer_id]
    for i in range(iproducer - 1, iconsumer, -1):
        node_id = tsorted_nodes[i]
        for edge in output_edges[node_id]:
            if edge is not None and edge.consumer_id in backward_node_ids:
                backward_node_ids.append(node_id)
                swappable_indices.append(i)
                break
    backward_node_ids.reverse()  # Recover ascending order.

    # Finally, put the affected nodes in their new positions.
    dest_indices = sorted(swappable_indices)
    reordered_ids = backward_node_ids + forward_node_ids
    for i, id in zip(dest_indices, reordered_ids, strict=True):
        tsorted_nodes[i] = id


def _update_edge_event_sets(
    nodes: list[tuple[str, Node]],
    tsorted_node_ids: list[int],
    input_edges: dict[int, list[_Edge | None]],
    output_edges: dict[int, list[_Edge | None]],
    start_node_id: int | None = None,
) -> None:
    # Update the edge event sets for all edges in 'output_edges'. If
    # 'start_node_id' is given, edges upstream of that node may be skipped for
    # efficiency (but this is not guaranteed).
    # Important: the _Edge objects are only accessed via 'input_edges' and
    # 'output_edges', not via 'nodes'. Thus, 'nodes' and 'tsorted_node_ids' are
    # not mutated directly. This allows using a (deep) copy of 'input_edges'
    # and 'output_edges' to perform the computation without modifying the
    # original graph (and without making a copy of 'nodes').

    if start_node_id is None:
        istart = 0
    else:
        istart = tsorted_node_ids.index(start_node_id)
    for node_id in tsorted_node_ids[istart:]:
        __, node = nodes[node_id]
        in_event_sets = [
            (e.event_set if e is not None else ())
            for e in input_edges[node_id]
        ]
        out_event_sets = node.map_event_sets(in_event_sets)
        for edge, event_set in zip(
            output_edges[node_id], out_event_sets, strict=True
        ):
            if edge is not None:
                edge.event_set = event_set


class Graph:
    """
    Processing graph.

    A processing graph is a directed acyclic graph of `Node` objects. Each node
    is also assigned a name for later reference and retrieval.

    Edges of the graph connect a specific output port of one node to a specific
    input port of another (in that direction). Any given input or output node
    may only have at most a single edge connected to it.

    At any point in time, the graph may contain nodes with unconnected input or
    output ports. These ports are the input and output ports of the graph as a
    whole.

    Note: It is not possible to add a connection between a graph input and a
    graph output that does not have an intermediate node. But this can be
    worked around by using a SelectAll (i.e., no-op) node.
    """

    def __init__(self) -> None:
        # List of (name, node). A node's id is its index in this append-only
        # list.
        self._nodes: list[tuple[str, Node]] = []

        # Index of nodes by name, mapped to id (index in self._nodes)
        self._node_name_index: dict[str, int] = {}

        # Nodes (by id) in a topologically sorted order.
        self._topo_sorted_node_ids: list[int] = []

        # Edges indexed by node id. The list has length equal to the number of
        # input/output ports of the node; its elements are edges where
        # connected; otherwise None.
        self._input_edge_index: dict[int, list[_Edge | None]] = {}
        self._output_edge_index: dict[int, list[_Edge | None]] = {}

    def _unique_node_name(self, basename: str) -> str:
        for i in itertools.count():
            name = f"{basename}-{i}"
            if name not in self._node_name_index:
                return name
        return ""  # Unreachable; to satisfy type checker.

    def add_node(
        self,
        name: str | None,
        node: Node,
        *,
        upstream: tuple[str, str] | str | None = None,
        downstream: tuple[str, str] | str | None = None,
    ) -> str:
        if name is None:
            node_name = self._unique_node_name(type(node).__name__)
        elif name in self._node_name_index:
            raise ValueError(f"node with name {name} already exists")
        else:
            node_name = name

        self._nodes.append((node_name, node))
        node_id = len(self._nodes) - 1
        self._node_name_index[node_name] = node_id
        self._topo_sorted_node_ids.append(node_id)
        self._input_edge_index[node_id] = [None] * len(node.inputs())
        self._output_edge_index[node_id] = [None] * len(node.outputs())

        if upstream is not None:
            self.connect(upstream, (node_name, "input"))
        if downstream is not None:
            self.connect((node_name, "output"), downstream)

        return node_name

    def connect(
        self, producer: tuple[str, str] | str, consumer: tuple[str, str] | str
    ) -> None:
        pname, pport = (
            producer if isinstance(producer, tuple) else (producer, "output")
        )
        cname, cport = (
            consumer if isinstance(consumer, tuple) else (consumer, "input")
        )
        ipnode = self._node_name_index[pname]
        icnode = self._node_name_index[cname]
        pnode: Node = self._nodes[ipnode][1]
        cnode: Node = self._nodes[icnode][1]
        ipport = pnode.outputs().index(pport)
        icport = cnode.inputs().index(cport)
        if self._input_edge_index[icnode][icport] is not None:
            raise ValueError("input port already connected")
        if self._output_edge_index[ipnode][ipport] is not None:
            raise ValueError("output port already connected")

        producer_input_edges = self._input_edge_index[ipnode]
        producer_input_event_sets = [
            (e.event_set if e is not None else ())
            for e in producer_input_edges
        ]
        event_set = pnode.map_event_sets(producer_input_event_sets)[ipport]

        edge = _Edge(ipnode, icnode, event_set)
        self._input_edge_index[icnode][icport] = edge
        self._output_edge_index[ipnode][ipport] = edge

        def rollback():
            self._input_edge_index[icnode][icport] = None
            self._output_edge_index[ipnode][ipport] = None

        try:
            _update_topological_order(
                self._topo_sorted_node_ids,
                self._input_edge_index,
                self._output_edge_index,
                edge,
            )
            _update_edge_event_sets(
                self._nodes,
                self._topo_sorted_node_ids,
                self._input_edge_index,
                self._output_edge_index,
                icnode,
            )
        except:
            rollback()
            raise

    def add_sequence(
        self,
        nodes: Sequence[tuple[str, Node] | Node],
        *,
        upstream: tuple[str, str] | str | None = None,
        downstream: tuple[str, str] | str | None = None,
    ) -> None:
        # Note: If nodes is empty, connects upstream to downstream.
        for node in nodes:
            nm, n = node if isinstance(node, tuple) else (None, node)
            if len(n.inputs()) != 1 or len(n.outputs()) != 1:
                raise ValueError(
                    "Graph.add_sequence() requires single-input, single-output nodes"
                )
            upstream = self.add_node(nm, n, upstream=upstream)
        if upstream is not None and downstream is not None:
            self.connect(upstream, downstream)

    def node(self, name: str) -> Node:
        split = name.split("/", 1)
        nm, node = self._nodes[self._node_name_index[split[0]]]
        if len(split) == 2:
            if not isinstance(node, Subgraph):
                raise LookupError(f"Node {split[0]} is not a sub-graph")
            return node.graph().node(split[1])
        return node

    def visit_nodes(self, visitor: Callable[[str, Node], None]) -> None:
        """
        Call the given visitor callable on every node.
        """
        for name, node in self._nodes:
            visitor(name, node)

    def _inputs(self) -> tuple[tuple[int, int], ...]:
        result: list[tuple[int, int]] = []
        for node_id in range(len(self._nodes)):
            input_edges = self._input_edge_index[node_id]
            result.extend(
                (node_id, i)
                for i, edge in enumerate(input_edges)
                if edge is None
            )
        return tuple(result)

    def inputs(self) -> tuple[tuple[str, str], ...]:
        result: list[tuple[str, str]] = []
        for node_id, (name, node) in enumerate(self._nodes):
            input_ports = node.inputs()
            input_edges = self._input_edge_index[node_id]
            for i, edge in enumerate(input_edges):
                if edge is None:
                    result.append((name, input_ports[i]))
        return tuple(result)

    def _outputs(self) -> tuple[tuple[int, int], ...]:
        result: list[tuple[int, int]] = []
        for node_id in range(len(self._nodes)):
            output_edges = self._output_edge_index[node_id]
            result.extend(
                (node_id, i)
                for i, edge in enumerate(output_edges)
                if edge is None
            )
        return tuple(result)

    def outputs(self) -> tuple[tuple[str, str], ...]:
        result: list[tuple[str, str]] = []
        for node_id, (name, node) in enumerate(self._nodes):
            output_ports = node.outputs()
            output_edges = self._output_edge_index[node_id]
            for i, edge in enumerate(output_edges):
                if edge is None:
                    result.append((name, output_ports[i]))
        return tuple(result)

    def map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        # Copy input edge index and add pseudo-edges for input event sets in
        # order to inject the given 'input_event_sets'.
        input_edges = deepcopy(self._input_edge_index)
        for (node_id, input_index), event_set in zip(
            self._inputs(), input_event_sets, strict=True
        ):
            input_edges[node_id][input_index] = _Edge(
                -1, node_id, tuple(event_set)
            )

        # Copy output edge index and add pseudo-edges in order to capture the
        # output event sets; also keep an ordered list for later retrieval.
        output_edges = deepcopy(self._output_edge_index)
        output_pseudo_edges: list[_Edge] = []
        for node_id, output_index in self._outputs():
            e = _Edge(node_id, -1, ())
            output_edges[node_id][output_index] = e
            output_pseudo_edges.append(e)

        _update_edge_event_sets(
            self._nodes,
            self._topo_sorted_node_ids,
            input_edges,
            output_edges,
        )

        return tuple(edge.event_set for edge in output_pseudo_edges)

    def generate_cpp(
        self,
        name_prefix: str,
        context_varname: str,
        params_varname: str,
        parameters: Mapping[str, str] = {},
        downstreams: Sequence[str] | None = None,
    ) -> str:
        downstreams = downstreams if downstreams is not None else ()
        external_names = [f"d{i}" for i in range(len(downstreams))]
        external_name_index = {
            (n, p): name
            for (n, p), name in zip(
                self._outputs(), external_names, strict=True
            )
        }
        internal_name_index: dict[tuple[int, int], str] = {}
        name_ctr = itertools.count()
        node_defs: list[str] = []
        for node_id in reversed(self._topo_sorted_node_ids):
            node_name, node = self._nodes[node_id]

            outputs: list[str] = []
            for i in range(len(node.outputs())):
                edge = self._output_edge_index[node_id][i]
                if edge is None:  # External downstream.
                    output = external_name_index[(node_id, i)]
                else:  # Internal downstream node.
                    consumer_input_idx = self._input_edge_index[
                        edge.consumer_id
                    ].index(edge)
                    output = internal_name_index[
                        (edge.consumer_id, consumer_input_idx)
                    ]
                outputs.append(f"std::move({output})")

            inputs: list[str] = []
            for i in range(len(node.inputs())):
                input = f"proc_{next(name_ctr)}"
                internal_name_index[(node_id, i)] = input
                inputs.append(input)

            node_code = node.generate_cpp(
                f"{name_prefix}/{node_name}",
                context_varname,
                params_varname,
                parameters,
                outputs,
            )
            if len(inputs) > 1:
                input_list = ", ".join(inputs)
                node_defs.append(f"auto [{input_list}] = {node_code};")
            else:
                node_defs.append(f"auto {inputs[0]} = {node_code};")

        input_refs = [
            f"std::move({internal_name_index[node_input]})"
            for node_input in self._inputs()
        ]
        input_ref_list = ", ".join(input_refs)
        input_ref_maybe_tuple = (
            f"std::tuple{{{input_ref_list}}}"
            if len(input_refs) != 1
            else input_ref_list
        )

        external_name_params = ", ".join(f"auto &&{d}" for d in external_names)
        downstream_args = ", ".join(downstreams)
        node_def_lines = "\n".join(node_defs)
        return dedent(f"""\
            [{context_varname}, &{params_varname}]({external_name_params}) {{
                {node_def_lines}
                return {input_ref_maybe_tuple};
            }}({downstream_args})""")


@final
class Subgraph(Node):
    """
    Node that wraps a nested Graph.

    Parameters
    ----------
    graph
        The subgraph to wrap
    input_map:
        Mapping from input port names of this node to (node, input_port) pairs
        of the graph. Any ports that are not mapped use the standard naming of
        node:input_port. Default: only use standard names.
    output_map:
        Mapping from output port names of this node to (node, output_port)
        pairs of the graph. Any ports that are not mapped use the standard
        naming of node:output_port. Default: only use standard names.
    """

    def __init__(
        self,
        graph: Graph,
        *,
        input_map: Mapping[str, tuple[str, str]] = {},
        output_map: Mapping[str, tuple[str, str]] = {},
    ) -> None:
        self._graph = deepcopy(graph)

        input_rmap = {v: k for k, v in input_map.items()}
        output_rmap = {v: k for k, v in output_map.items()}

        super().__init__(
            input=tuple(
                input_rmap.get((n, p), f"{n}:{p}")
                for n, p in self._graph.inputs()
            ),
            output=tuple(
                output_rmap.get((n, p), f"{n}:{p}")
                for n, p in self._graph.outputs()
            ),
        )

    def graph(self) -> Graph:
        return self._graph

    @override
    def map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        return self._graph.map_event_sets(input_event_sets)

    @override
    def generate_cpp(
        self,
        node_name: str,
        context_varname: str,
        params_varname: str,
        parameters: Mapping[str, str],
        downstreams: Sequence[str],
    ) -> str:
        return self._graph.generate_cpp(
            node_name, context_varname, params_varname, parameters, downstreams
        )
