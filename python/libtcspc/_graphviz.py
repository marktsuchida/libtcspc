# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import itertools

from ._graph import Graph, Subgraph, _ThreadGroup
from ._node import Node

# Graphviz X11 color names used to distinguish thread groups, cycled if there
# are more groups than colors. Red is deliberately excluded; it marks nodes
# with a thread-affinity conflict.
_THREAD_PALETTE = (
    "blue",
    "darkgreen",
    "purple",
    "orange",
    "brown",
    "deeppink",
    "teal",
    "darkgoldenrod",
)


def to_graphviz(graph: Graph, *, flatten: bool) -> str:
    port_groups, conflicts = graph._thread_group_port_map()
    conflict_node_names = {qual_name for qual_name, _ in conflicts}

    # Assign a stable color to each thread group, in first-appearance order
    # over the (deterministic) recording walk.
    group_colors: dict[_ThreadGroup, str] = {}
    for group in port_groups.values():
        if group not in group_colors:
            group_colors[group] = _THREAD_PALETTE[
                len(group_colors) % len(_THREAD_PALETTE)
            ]

    def port_color(
        path_name: tuple[tuple[str, ...], str], port: str
    ) -> str | None:
        path, name = path_name
        group = port_groups.get((path, name, port))
        return group_colors[group] if group is not None else None

    counter = itertools.count()
    id_map: dict[tuple[tuple[str, ...], str], str] = {}
    nodes_by_path: dict[tuple[tuple[str, ...], str], Node] = {}

    def assign_ids(g: Graph, path: tuple[str, ...]) -> None:
        for name, node in g._iter_nodes():
            id_map[(path, name)] = f"n{next(counter)}"
            nodes_by_path[(path, name)] = node
            if isinstance(node, Subgraph):
                assign_ids(node.graph(), path + (name,))

    assign_ids(graph, ())

    def resolve_producer(
        path: tuple[str, ...], name: str, port: str
    ) -> tuple[tuple[tuple[str, ...], str], str] | None:
        node = nodes_by_path.get((path, name))
        if node is None:
            return None
        if isinstance(node, Subgraph):
            inner_name, inner_port = node._output_source(port)
            return resolve_producer(path + (name,), inner_name, inner_port)
        return ((path, name), port)

    def resolve_consumer(
        path: tuple[str, ...], name: str, port: str
    ) -> tuple[tuple[tuple[str, ...], str], str] | None:
        node = nodes_by_path.get((path, name))
        if node is None:
            return None
        if isinstance(node, Subgraph):
            inner_name, inner_port = node._input_target(port)
            return resolve_consumer(path + (name,), inner_name, inner_port)
        return ((path, name), port)

    lines: list[str] = ["digraph {"]

    def emit_node_line(
        dot_id: str, name: str, node: Node, qual_name: str, indent: str
    ) -> None:
        cls = type(node).__name__
        attrs = f'shape=box, label="{cls}\\n{name}"'
        if qual_name in conflict_node_names:
            attrs += ", color=red, penwidth=2"
        lines.append(f"{indent}{dot_id} [{attrs}];")

    def emit_edge(
        pid: str,
        cid: str,
        pport: str,
        cport: str,
        pnode: Node,
        cnode: Node,
        color: str | None,
        indent: str,
    ) -> None:
        attrs: list[str] = []
        if len(pnode.outputs()) > 1:
            attrs.append(f'taillabel="{pport}"')
        if len(cnode.inputs()) > 1:
            attrs.append(f'headlabel="{cport}"')
        if color is not None:
            attrs.append(f'color="{color}"')
            attrs.append("penwidth=2")
        attr_str = f" [{', '.join(attrs)}]" if attrs else ""
        lines.append(f"{indent}{pid} -> {cid}{attr_str};")

    def walk(g: Graph, path: tuple[str, ...], indent: str) -> None:
        for name, node in g._iter_nodes():
            dot_id = id_map[(path, name)]
            if isinstance(node, Subgraph):
                if flatten:
                    walk(node.graph(), path + (name,), indent)
                else:
                    lines.append(f"{indent}subgraph cluster_{dot_id} {{")
                    lines.append(f'{indent}  label="{name}";')
                    walk(node.graph(), path + (name,), indent + "  ")
                    lines.append(f"{indent}}}")
            else:
                qual_name = "/".join(path + (name,))
                emit_node_line(dot_id, name, node, qual_name, indent)

        for prod_name, prod_port, cons_name, cons_port in g._iter_edges():
            prod = resolve_producer(path, prod_name, prod_port)
            cons = resolve_consumer(path, cons_name, cons_port)
            if prod is None or cons is None:
                continue
            (ppath_name, pport) = prod
            (cpath_name, cport) = cons
            pid = id_map.get(ppath_name)
            cid = id_map.get(cpath_name)
            if pid is None or cid is None:
                continue
            pnode = nodes_by_path[ppath_name]
            cnode = nodes_by_path[cpath_name]
            color = port_color(ppath_name, pport)
            emit_edge(pid, cid, pport, cport, pnode, cnode, color, indent)

    walk(graph, (), "  ")

    for name, port in graph.inputs():
        marker_id = f"m{next(counter)}"
        cons = resolve_consumer((), name, port)
        if cons is None:
            continue
        (cpath_name, cport) = cons
        cid = id_map.get(cpath_name)
        if cid is None:
            continue
        cnode = nodes_by_path[cpath_name]
        lines.append(f'  {marker_id} [shape=point, xlabel="{name}:{port}"];')
        attrs: list[str] = []
        if len(cnode.inputs()) > 1:
            attrs.append(f'headlabel="{cport}"')
        color = port_color(cpath_name, cport)
        if color is not None:
            attrs.append(f'color="{color}"')
            attrs.append("penwidth=2")
        attr_str = f" [{', '.join(attrs)}]" if attrs else ""
        lines.append(f"  {marker_id} -> {cid}{attr_str};")

    for name, port in graph.outputs():
        marker_id = f"m{next(counter)}"
        prod = resolve_producer((), name, port)
        if prod is None:
            continue
        (ppath_name, pport) = prod
        pid = id_map.get(ppath_name)
        if pid is None:
            continue
        pnode = nodes_by_path[ppath_name]
        lines.append(f'  {marker_id} [shape=point, xlabel="{name}:{port}"];')
        attrs = []
        if len(pnode.outputs()) > 1:
            attrs.append(f'taillabel="{pport}"')
        color = port_color(ppath_name, pport)
        if color is not None:
            attrs.append(f'color="{color}"')
            attrs.append("penwidth=2")
        attr_str = f" [{', '.join(attrs)}]" if attrs else ""
        lines.append(f"  {pid} -> {marker_id}{attr_str};")

    lines.append("}")
    return "\n".join(lines)
