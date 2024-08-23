# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools
from collections.abc import Iterable
from copy import deepcopy
from textwrap import dedent
from typing import Any

import cppyy

from ._access import Access, Accessible
from ._graph import Graph

cppyy.include("libtcspc/tcspc.hpp")

cppyy.include("memory")
cppyy.include("type_traits")
cppyy.include("utility")

cppyy.cppdef(
    dedent("""\
    namespace tcspc::py::context {
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
    context_varname: str,
    event_types: tuple[str, ...],
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
    )
    return fname, dedent(f"""\
        namespace tcspc::py::context {{
            template <typename Downstream> class {input_proc} {{
                Downstream downstream;

            public:
                explicit {input_proc}(Downstream &&downstream)
                : downstream(std::move(downstream)) {{}}

                {handlers}

                void flush() {{ downstream.flush(); }}
            }};

            auto {fname}(std::shared_ptr<tcspc::context> {context_varname}) {{
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
    graph_code: str, context_varname: str, event_types: tuple[str, ...]
) -> Any:
    ctr = next(_cpp_name_counter)
    fname, code = _instantiate_func(
        graph_code, context_varname, event_types, ctr
    )
    cppyy.cppdef(code)
    return getattr(cppyy.gbl.tcspc.py.context, fname)


def _collect_access_tags(graph: Graph) -> dict[str, type[Access]]:
    accesses: list[tuple[str, type[Access]]] = []

    def visit(name: str, node: Accessible):
        accesses.extend(node.accesses())

    graph.visit_nodes(visit)
    return dict(accesses)


class CompiledGraph:
    """
    A compiled graph, which can be reused for multiple executions.

    This is an immutable object.

    Parameters
    ----------
    graph: Graph
        The graph to compile. The graph must have exactly one input port and no
        output ports.
    event_cpptypes: Iterable[str]
        The C++ type names accepted as events (via `handle()`) from Python.
        As a special case, `span<T>` is treated the same as `span<T const>`.
    """

    # TODO Event types should be Python types, which we transform to C++ types.
    def __init__(
        self, graph: Graph, event_cpptypes: Iterable[str] = ()
    ) -> None:
        n_in, n_out = len(graph.inputs()), len(graph.outputs())
        if n_in != 1:
            raise ValueError(
                f"graph is not executable (must have exactly 1 input port; found {n_in})"
            )
        if n_out > 0:
            raise ValueError(
                f"graph is not executable (must have no output ports; found {n_out})"
            )
        ctx_var = "ctx"
        code = graph.generate_cpp("", ctx_var)
        evt_types = tuple(event_cpptypes)
        self._instantiator = _compile_instantiator(code, ctx_var, evt_types)
        self._access_types = _collect_access_tags(graph)

    def _instantiate(self, cpp_context) -> Any:
        # Not in API but called by ExecutionContext.
        return self._instantiator(cpp_context)

    def access_types(self) -> dict[str, type[Access]]:
        return deepcopy(self._access_types)
