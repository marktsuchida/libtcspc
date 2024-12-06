# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "Graph",
    "Subgraph",
]

import itertools
from collections.abc import Callable, Collection, Mapping, Sequence
from copy import deepcopy
from dataclasses import dataclass
from textwrap import dedent
from typing import final

from typing_extensions import override

from ._access import Access, Accessible
from ._codegen import CodeGenerationContext
from ._cpp_utils import CppExpression, CppIdentifier, CppTypeName
from ._events import EventType
from ._node import Node
from ._param import Param, Parameterized


@dataclass
class _Edge:
    # Edge connecting nodes in a Graph. This is a private type because it only
    # has meaning in the internal context of a Graph.
    producer_id: int
    consumer_id: int
    event_set: tuple[EventType, ...]  # Updated when an upstream edge is added


def _update_topological_order(
    tsorted_nodes: list[int],
    input_edges: Mapping[int, Sequence[_Edge | None]],
    output_edges: Mapping[int, Sequence[_Edge | None]],
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

    def cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstreams: Sequence[CppExpression] | None = None,
    ) -> CppExpression:
        downstreams = downstreams if downstreams is not None else ()
        external_names = [
            CppIdentifier(f"d{i}") for i in range(len(downstreams))
        ]
        external_name_index: dict[tuple[int, int], CppIdentifier] = {
            (n, p): name
            for (n, p), name in zip(
                self._outputs(), external_names, strict=True
            )
        }
        internal_name_index: dict[tuple[int, int], CppIdentifier] = {}
        name_ctr = itertools.count()
        node_defs: list[str] = []
        for node_id in reversed(self._topo_sorted_node_ids):
            _, node = self._nodes[node_id]

            outputs: list[CppExpression] = []
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
                outputs.append(CppExpression(f"std::move({output})"))

            inputs: list[CppIdentifier] = []
            for i in range(len(node.inputs())):
                input = CppIdentifier(f"proc_{next(name_ctr)}")
                internal_name_index[(node_id, i)] = input
                inputs.append(input)

            node_code = node.cpp_expression(gencontext, outputs)
            if len(inputs) > 1:
                input_list = ", ".join(inputs)
                node_defs.append(f"auto [{input_list}] = {node_code};")
            else:
                node_defs.append(f"auto {inputs[0]} = {node_code};")

        input_names: list[CppIdentifier] = [
            internal_name_index[node_input] for node_input in self._inputs()
        ]
        moved_input_names = ", ".join(f"std::move({n})" for n in input_names)
        return_expr = (
            CppExpression(input_names[0])  # No move needed due to NRVO.
            if len(input_names) == 1
            else (CppExpression(f"std::tuple{{{moved_input_names}}}"))
        )
        external_name_params = ", ".join(f"auto &&{d}" for d in external_names)
        downstream_args = ", ".join(downstreams)
        node_def_lines = "\n".join(node_defs)
        captures = ", ".join(
            (gencontext.context_varname, f"&{gencontext.params_varname}")
        )
        return CppExpression(
            dedent(f"""\
                [{captures}]({external_name_params}) {{
                    {node_def_lines}
                    return {return_expr};
                }}({downstream_args})""")
        )

    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        params: list[tuple[Param, CppTypeName]] = []

        def visit(node_name: str, node: Parameterized):
            params.extend(node.parameters())

        self.visit_nodes(visit)

        if len(params) > len(set(p.name for p, _ in params)):
            param_nodes: dict[CppIdentifier, list[str]] = {}

            def visit(node_name: str, node: Parameterized):
                for param, _ in node.parameters():
                    param_nodes.setdefault(param.name, []).append(node_name)

            self.visit_nodes(visit)

            for param, node_names in (
                (p, ns) for p, ns in param_nodes.items() if len(ns) > 1
            ):
                strnames = ", ".join(node_names)
                raise ValueError(
                    f"graph contains duplicate parameter {param} in nodes {strnames}"
                )

        return params

    def accesses(self) -> Sequence[tuple[str, type[Access]]]:
        accesses: list[tuple[str, type[Access]]] = []

        def visit(node_name: str, node: Accessible):
            accesses.extend(node.accesses())

        self.visit_nodes(visit)

        if len(accesses) > len(set(t for t, _ in accesses)):
            tag_nodes: dict[str, list[str]] = {}

            def visit(node_name: str, node: Accessible):
                for tag, _ in node.accesses():
                    tag_nodes.setdefault(tag, []).append(node_name)

            self.visit_nodes(visit)

            for tag, node_names in (
                (t, ns) for t, ns in tag_nodes.items() if len(ns) > 1
            ):
                strnames = ", ".join(node_names)
                raise ValueError(
                    f"graph contains duplicate access tag {tag} in nodes {strnames}"
                )

        return accesses


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
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        return self._graph.parameters()

    @override
    def accesses(self) -> Sequence[tuple[str, type[Access]]]:
        return self._graph.accesses()

    @override
    def cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstreams: Sequence[CppExpression],
    ) -> CppExpression:
        return self._graph.cpp_expression(gencontext, downstreams)
