# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools
from collections.abc import Iterable
from textwrap import dedent
from typing import Any

import cppyy

from ._access import Access, Accessible
from ._cpp_utils import CppIdentifier, CppTypeName
from ._events import EventType
from ._graph import CodeGenerationContext, Graph
from ._param import Parameterized

cppyy.include("libtcspc/tcspc.hpp")

cppyy.include("memory")
cppyy.include("type_traits")
cppyy.include("utility")


class CompiledGraph:
    """
    A compiled graph.

    Use `compile_graph()` to obtain an instance. Direct instantiaiton form user
    code is not supported.

    Objects of this type should be treated as immutable.
    """

    def __init__(
        self,
        instantiator: Any,
        access_types: Iterable[tuple[str, type[Access]]],
        param_struct: Any,
        params: list[tuple[str, CppTypeName, Any]],
    ) -> None:
        self._instantiator = instantiator
        self._access_types = tuple(access_types)
        self._param_struct = param_struct
        self._params = params

    def access_types(self) -> tuple[tuple[str, type[Access]], ...]:
        return self._access_types


def _param_struct(
    param_types: Iterable[tuple[str, CppTypeName]], ctr: int
) -> tuple[str, str]:
    tname = f"params_{ctr}"
    fields = "\n".join(
        f"""\
                {typ} {name};"""
        for name, typ in param_types
    ).lstrip()
    return tname, dedent(f"""\
        namespace tcspc::py::compile {{
            struct {tname} {{
                {fields}
            }};
        }}""")


cppyy.cppdef(
    dedent("""\
    namespace tcspc::py::compile {
        template <typename T> struct make_event_arg {
            using type = std::remove_cv_t<std::remove_reference_t<T>> const &;
        };

        template <typename E> struct make_event_arg<span<E>> {
            // No need for 'const &', pass 'span' by value.
            using type = span<std::remove_cv_t<E> const>;
        };

        template <typename T>
        using make_event_arg_t = typename make_event_arg<T>::type;
    }""")
)


def _instantiate_func(
    graph_code: str,
    gencontext: CodeGenerationContext,
    event_types: Iterable[CppTypeName],
    ctr: int,
) -> tuple[str, str]:
    input_proc = f"input_processor_{ctr}"
    fname = f"instantiate_graph_{ctr}"
    handlers = "\n\n".join(
        f"""\
                void handle(make_event_arg_t<{event_type}> event) {{
                    downstream.handle(event);
                }}"""
        for event_type in event_types
    ).lstrip()
    return fname, dedent(f"""\
        namespace tcspc::py::compile {{
            template <typename Downstream> class {input_proc} {{
                Downstream downstream;

            public:
                explicit {input_proc}(Downstream &&downstream)
                : downstream(std::move(downstream)) {{}}

                {handlers}

                void flush() {{ downstream.flush(); }}
            }};

            auto {fname}(std::shared_ptr<tcspc::context> {gencontext.context_varname},
                         params_{ctr} const &{gencontext.params_varname}) {{
                return {input_proc}({graph_code});
            }}
        }}""")


_cpp_name_counter = itertools.count()


# Define the function to create a processor instance for the give graph. Wrap
# the input processor so that it handles exactly the requested set of event
# types. The wrapping also ensures that handle() is a non-template overload
# set. Return the instantiator function, which takes the shared_ptr<context>
# and returns the processor.
@functools.cache
def _compile_instantiator(
    graph_code: str,
    gencontext: CodeGenerationContext,
    param_types: Iterable[tuple[str, CppTypeName]],
    event_types: Iterable[CppTypeName],
) -> tuple[str, Any]:
    ctr = next(_cpp_name_counter)

    struct_name, struct_code = _param_struct(param_types, ctr)
    cppyy.cppdef(struct_code)

    fname, code = _instantiate_func(graph_code, gencontext, event_types, ctr)
    cppyy.cppdef(code)

    return (
        getattr(cppyy.gbl.tcspc.py.compile, struct_name),
        getattr(cppyy.gbl.tcspc.py.compile, fname),
    )


def _collect_params(graph: Graph) -> list[tuple[str, CppTypeName, Any]]:
    params: list[tuple[str, CppTypeName, Any]] = []

    def visit(node_name: str, node: Parameterized):
        params.extend(node.parameters())

    graph.visit_nodes(visit)

    if len(params) > len(set(p for p, _, _ in params)):
        param_nodes: dict[str, list[str]] = {}

        def visit(node_name: str, node: Parameterized):
            for param, _, _ in node.parameters():
                param_nodes.setdefault(param, []).append(node_name)

        graph.visit_nodes(visit)

        for param, node_names in (
            (p, ns) for p, ns in param_nodes.items() if len(ns) > 1
        ):
            strnames = ", ".join(node_names)
            raise ValueError(
                f"graph contains duplicate parameter {param} in nodes {strnames}"
            )

    return params


def _collect_access_tags(graph: Graph) -> list[tuple[str, type[Access]]]:
    accesses: list[tuple[str, type[Access]]] = []

    def visit(node_name: str, node: Accessible):
        accesses.extend(node.accesses())

    graph.visit_nodes(visit)

    if len(accesses) > len(set(t for t, _ in accesses)):
        tag_nodes: dict[str, list[str]] = {}

        def visit(node_name: str, node: Accessible):
            for tag, _ in node.accesses():
                tag_nodes.setdefault(tag, []).append(node_name)

        graph.visit_nodes(visit)

        for tag, node_names in (
            (t, ns) for t, ns in tag_nodes.items() if len(ns) > 1
        ):
            strnames = ", ".join(node_names)
            raise ValueError(
                f"graph contains duplicate access tag {tag} in nodes {strnames}"
            )

    return accesses


def compile_graph(
    graph: Graph, input_event_types: Iterable[EventType] = ()
) -> CompiledGraph:
    """
    Compile a graph. The result can be used for multiple executions.

    Parameters
    ----------
    graph: Graph
        The graph to compile. The graph must have exactly one input port and no
        output ports.
    input_event_types: Iterable[EventType]
        The (Python) event types accepted as input (via `handle()`).

    Returns
    -------
    CompiledGraph
        The compiled graph.
    """

    n_in, n_out = len(graph.inputs()), len(graph.outputs())
    if n_in != 1:
        raise ValueError(
            f"graph is not executable (must have exactly 1 input port; found {n_in})"
        )
    if n_out > 0:
        raise ValueError(
            f"graph is not executable (must have no output ports; found {n_out})"
        )

    params = _collect_params(graph)
    param_types = ((name, cpp_type) for name, cpp_type, default in params)
    genctx = CodeGenerationContext(
        CppIdentifier("ctx"), CppIdentifier("params")
    )
    code = graph.generate_cpp(genctx)
    param_struct, instantiator = _compile_instantiator(
        code, genctx, param_types, (e.cpp_type for e in input_event_types)
    )
    access_types = _collect_access_tags(graph)
    return CompiledGraph(instantiator, access_types, param_struct, params)
