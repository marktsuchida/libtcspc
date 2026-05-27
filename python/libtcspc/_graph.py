# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import itertools
from collections.abc import Callable, Collection, Mapping, Sequence
from copy import deepcopy
from dataclasses import dataclass
from typing import Any, final

from typing_extensions import override

from ._access import AccessTag, _Accessible, _AccessorSpec
from ._codegen import _CodeGenerationContext
from ._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from ._events import EventType
from ._node import Node
from ._param import Param, _Parameterized


@dataclass(eq=False)
class _Edge:
    # Edge connecting nodes in a Graph. This is a private type because it only
    # has meaning in the internal context of a Graph. Identity equality
    # (eq=False) is required: parallel edges between the same producer and
    # consumer have identical fields but are distinct connections, and codegen
    # locates an edge's port via list.index().
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
        out_event_sets = node._map_event_sets(in_event_sets)
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
    input port of another (in that direction). Any given input or output port
    may have at most a single edge connected to it.

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
        upstream: tuple[str, str]
        | str
        | Mapping[str, tuple[str, str] | str]
        | None = None,
        downstream: tuple[str, str]
        | str
        | Mapping[str, tuple[str, str] | str]
        | None = None,
    ) -> str:
        """
        Add a node to the graph, optionally connecting it to an existing node.

        Parameters
        ----------
        name : str or None
            Name under which the node will be retrievable. If ``None``,
            an auto-generated name of the form ``"{ClassName}-{N}"`` is
            assigned, where ``N`` is the smallest non-negative integer
            making the name unique.
        node : Node
            The node to add.
        upstream : tuple[str, str] or str or Mapping or None
            If given, connect ``upstream`` to the new node's ``"input"``
            port. A ``(node_name, port_name)`` tuple selects an explicit
            output port; a bare node name defaults to its ``"output"``
            port. May also be a mapping from this node's **input** port
            names to connection sources, connecting several inputs at
            once; each value follows the same shorthand (bare name →
            that node's ``"output"`` port, or an explicit
            ``(name, port)`` tuple).
        downstream : tuple[str, str] or str or Mapping or None
            If given, connect the new node's ``"output"`` port to
            ``downstream``. A ``(node_name, port_name)`` tuple selects
            an explicit input port; a bare node name defaults to its
            ``"input"`` port. May also be a mapping from this node's
            **output** port names to connection targets, connecting
            several outputs at once; each value follows the same
            shorthand (bare name → that node's ``"input"`` port, or an
            explicit ``(name, port)`` tuple).

        Returns
        -------
        str
            The name under which the node was added (the auto-generated
            name if ``name`` was ``None``, otherwise ``name``).

        Raises
        ------
        ValueError
            If ``name`` already exists in the graph, if a requested
            connection's port types do not match, or if a requested
            connection would introduce a cycle.

        Notes
        -----
        If any requested connection fails, the node and all connections
        made by the call are removed and the exception propagates,
        leaving the graph unchanged.
        """
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

        upstream_conns: list[tuple[tuple[str, str] | str, str]]
        if isinstance(upstream, Mapping):
            upstream_conns = [
                (src, in_port) for in_port, src in upstream.items()
            ]
        elif upstream is None:
            upstream_conns = []
        else:
            upstream_conns = [(upstream, "input")]

        downstream_conns: list[tuple[str, tuple[str, str] | str]]
        if isinstance(downstream, Mapping):
            downstream_conns = [
                (out_port, dst) for out_port, dst in downstream.items()
            ]
        elif downstream is None:
            downstream_conns = []
        else:
            downstream_conns = [("output", downstream)]

        try:
            for src, in_port in upstream_conns:
                self.connect(src, (node_name, in_port))
            for out_port, dst in downstream_conns:
                self.connect((node_name, out_port), dst)
        except:
            self._remove_last_node()
            raise

        return node_name

    def _remove_last_node(self) -> None:
        # Remove the most recently added node and any edges incident to it.
        # Only the last node can be removed without shifting other node ids
        # (which are indices into the append-only self._nodes).
        node_id = len(self._nodes) - 1
        for edge in self._input_edge_index[node_id]:
            if edge is not None:
                slots = self._output_edge_index[edge.producer_id]
                for i, e in enumerate(slots):
                    if e is edge:
                        slots[i] = None
                        break
        for edge in self._output_edge_index[node_id]:
            if edge is not None:
                slots = self._input_edge_index[edge.consumer_id]
                for i, e in enumerate(slots):
                    if e is edge:
                        slots[i] = None
                        break
        del self._input_edge_index[node_id]
        del self._output_edge_index[node_id]
        self._topo_sorted_node_ids.remove(node_id)
        name, _ = self._nodes.pop()
        del self._node_name_index[name]

    def connect(
        self, producer: tuple[str, str] | str, consumer: tuple[str, str] | str
    ) -> None:
        """
        Connect an output port to an input port.

        Parameters
        ----------
        producer : tuple[str, str] or str
            A ``(node_name, port_name)`` tuple selecting the producing
            node and its output port. A bare node name is shorthand for
            ``(name, "output")``.
        consumer : tuple[str, str] or str
            A ``(node_name, port_name)`` tuple selecting the consuming
            node and its input port. A bare node name is shorthand for
            ``(name, "input")``.

        Raises
        ------
        ValueError
            If either port is already connected, if the producer's
            output event set is not compatible with the consumer's
            input, or if the connection would introduce a cycle.

        Notes
        -----
        Validation is performed immediately. If the connection is
        rejected, the graph is restored to its previous state before
        the exception propagates.
        """
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
        event_set = pnode._map_event_sets(producer_input_event_sets)[ipport]

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

    def add_chain(
        self,
        nodes: Sequence[tuple[str, Node] | Node],
        *,
        upstream: tuple[str, str] | str | None = None,
        downstream: tuple[str, str] | str | None = None,
    ) -> None:
        """
        Add a chain of single-input single-output nodes connected in series.

        The last node may be a sink — that is, it may have zero outputs in
        addition to its single input. The first and middle nodes must have
        exactly one input and one output.

        Parameters
        ----------
        nodes : Sequence[tuple[str, Node] or Node]
            The nodes to add, in order. Each element is either a
            ``(name, node)`` tuple or a bare `Node`; in the latter case
            a name is auto-generated as by `add_node`.
        upstream : tuple[str, str] or str or None
            If given, connect ``upstream`` to the first added node's
            input port. The shorthand rules of `add_node` apply.
        downstream : tuple[str, str] or str or None
            If given, connect the last added node's output port to
            ``downstream``. The shorthand rules of `add_node` apply.
            Must be ``None`` if the last node is a sink (zero outputs).

        Raises
        ------
        ValueError
            If the first or any middle node does not have exactly one input
            and one output; if the last node does not have exactly one input
            and (zero or one) outputs; if ``downstream`` is given but the
            last node has zero outputs; or if any requested connection
            fails.

        Notes
        -----
        If ``nodes`` is empty and both ``upstream`` and ``downstream``
        are given, ``upstream`` is connected directly to ``downstream``.

        If any requested connection or node fails, all nodes and
        connections made by the call are removed and the exception
        propagates, leaving the graph unchanged.
        """
        initial_node_count = len(self._nodes)
        try:
            nodes_list = list(nodes)
            last_index = len(nodes_list) - 1
            for i, node in enumerate(nodes_list):
                nm, n = node if isinstance(node, tuple) else (None, node)
                n_in, n_out = len(n.inputs()), len(n.outputs())
                is_last = i == last_index
                if is_last:
                    if n_in != 1 or n_out > 1:
                        raise ValueError(
                            "Graph.add_chain() last node must have exactly "
                            "one input and zero or one outputs; "
                            f"got {n_in} input(s) and {n_out} output(s)"
                        )
                    if n_out == 0 and downstream is not None:
                        raise ValueError(
                            "Graph.add_chain() last node has zero outputs; "
                            "downstream= must not be given"
                        )
                else:
                    if n_in != 1 or n_out != 1:
                        position = "first" if i == 0 else "middle"
                        raise ValueError(
                            f"Graph.add_chain() {position} node must have "
                            "exactly one input and one output; "
                            f"got {n_in} input(s) and {n_out} output(s)"
                        )
                upstream = self.add_node(nm, n, upstream=upstream)
                if is_last and n_out == 0:
                    upstream = None
            if upstream is not None and downstream is not None:
                self.connect(upstream, downstream)
        except:
            while len(self._nodes) > initial_node_count:
                self._remove_last_node()
            raise

    def node(self, name: str) -> Node:
        split = name.split("/", 1)
        nm, node = self._nodes[self._node_name_index[split[0]]]
        if len(split) == 2:
            if not isinstance(node, Subgraph):
                raise LookupError(f"Node {split[0]} is not a sub-graph")
            return node.graph().node(split[1])
        return node

    def _visit_nodes(self, visitor: Callable[[str, Node], None]) -> None:
        """
        Invoke ``visitor`` on every node in the graph.

        Parameters
        ----------
        visitor : Callable[[str, Node], None]
            Called once per node with ``(node_name, node)``. The return
            value is ignored.

        Notes
        -----
        Traversal is in node-addition order, not topological order.
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
        """
        Return the graph's external input ports — those without an incoming edge.

        Returns
        -------
        tuple[tuple[str, str], ...]
            A tuple of ``(node_name, port_name)`` pairs, in
            node-addition order.
        """
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
        """
        Return the graph's external output ports — those without an outgoing edge.

        Returns
        -------
        tuple[tuple[str, str], ...]
            A tuple of ``(node_name, port_name)`` pairs, in
            node-addition order.
        """
        result: list[tuple[str, str]] = []
        for node_id, (name, node) in enumerate(self._nodes):
            output_ports = node.outputs()
            output_edges = self._output_edge_index[node_id]
            for i, edge in enumerate(output_edges):
                if edge is None:
                    result.append((name, output_ports[i]))
        return tuple(result)

    def _map_event_sets(
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

    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression] | None = None,
    ) -> _CppExpression:
        downstreams = downstreams if downstreams is not None else ()
        external_names = [
            _CppIdentifier(f"d{i}") for i in range(len(downstreams))
        ]
        external_name_index: dict[tuple[int, int], _CppIdentifier] = {
            (n, p): name
            for (n, p), name in zip(
                self._outputs(), external_names, strict=True
            )
        }
        internal_name_index: dict[tuple[int, int], _CppIdentifier] = {}
        name_ctr = itertools.count()
        node_defs: list[str] = []
        for node_id in reversed(self._topo_sorted_node_ids):
            _, node = self._nodes[node_id]

            outputs: list[_CppExpression] = []
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
                outputs.append(_CppExpression(f"std::move({output})"))

            inputs: list[_CppIdentifier] = []
            for i in range(len(node.inputs())):
                input = _CppIdentifier(f"proc_{next(name_ctr)}")
                internal_name_index[(node_id, i)] = input
                inputs.append(input)

            node_code = node._cpp_expression(gencontext, outputs)
            if len(inputs) > 1:
                input_list = ", ".join(inputs)
                node_defs.append(f"auto [{input_list}] = {node_code};")
            else:
                node_defs.append(f"auto {inputs[0]} = {node_code};")

        input_names: list[_CppIdentifier] = [
            internal_name_index[node_input] for node_input in self._inputs()
        ]
        moved_input_names = ", ".join(f"std::move({n})" for n in input_names)
        return_expr = (
            _CppExpression(input_names[0])  # No move needed due to NRVO.
            if len(input_names) == 1
            else (_CppExpression(f"std::tuple{{{moved_input_names}}}"))
        )
        external_name_params = ", ".join(f"auto &&{d}" for d in external_names)
        comma_downstream_args = (
            "".join(f", {ds}" for ds in downstreams)
            if len(downstreams)
            else ""
        )
        node_def_lines = "\n".join(node_defs)
        captures = ", ".join(
            (gencontext.context_varname, f"&{gencontext.params_varname}")
        )
        return _CppExpression(
            f"""std::invoke([{captures}]({external_name_params}) {{
                    {node_def_lines}
                    return {return_expr};
                }}{comma_downstream_args})"""
        )

    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []

        def visit(node_name: str, node: _Parameterized):
            params.extend(node._parameters())

        self._visit_nodes(visit)

        if len(params) > len(set(p.name for p, _ in params)):
            param_nodes: dict[str, list[str]] = {}

            def visit(node_name: str, node: _Parameterized):
                for param, _ in node._parameters():
                    param_nodes.setdefault(param.name, []).append(node_name)

            self._visit_nodes(visit)

            for param, node_names in (
                (p, ns) for p, ns in param_nodes.items() if len(ns) > 1
            ):
                strnames = ", ".join(node_names)
                raise ValueError(
                    f"graph contains duplicate parameter {param} in nodes {strnames}"
                )

        return params

    def _param_encoders(self) -> dict[str, Callable[[Any], Any]]:
        encoders: dict[str, Callable[[Any], Any]] = {}

        def visit(node_name: str, node: _Parameterized):
            encoders.update(node._param_encoders())

        self._visit_nodes(visit)
        return encoders

    def _accesses(self) -> Sequence[tuple[AccessTag, _AccessorSpec]]:
        accesses: list[tuple[AccessTag, _AccessorSpec]] = []

        def visit(node_name: str, node: _Accessible):
            accesses.extend(node._accesses())

        self._visit_nodes(visit)

        if len(accesses) > len(set(t for t, _ in accesses)):
            tag_nodes: dict[AccessTag, list[str]] = {}

            def visit(node_name: str, node: _Accessible):
                for tag, _ in node._accesses():
                    tag_nodes.setdefault(tag, []).append(node_name)

            self._visit_nodes(visit)

            for tag, node_names in (
                (t, ns) for t, ns in tag_nodes.items() if len(ns) > 1
            ):
                strnames = ", ".join(node_names)
                raise ValueError(
                    f"graph contains duplicate access tag {tag} in nodes {strnames}"
                )

        return accesses

    def _value_event_types(self) -> Sequence[EventType]:
        types: list[EventType] = []

        def visit(node_name: str, node: Node):
            types.extend(node._value_event_types())

        self._visit_nodes(visit)
        return types


@final
class Subgraph(Node):
    """
    Node that wraps a nested Graph.

    Parameters
    ----------
    graph
        The subgraph to wrap.
    input_map
        Mapping from input port names of this node to (node, input_port) pairs
        of the graph. Any ports that are not mapped use the standard naming of
        node:input_port. Default: only use standard names.
    output_map
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
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        return self._graph._map_event_sets(input_event_sets)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._graph._parameters()

    @override
    def _param_encoders(self) -> dict[str, Callable[[Any], Any]]:
        return self._graph._param_encoders()

    @override
    def _accesses(self) -> Sequence[tuple[AccessTag, _AccessorSpec]]:
        return self._graph._accesses()

    @override
    def _value_event_types(self) -> Sequence[EventType]:
        return self._graph._value_event_types()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        return self._graph._cpp_expression(gencontext, downstreams)
