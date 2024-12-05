# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools
import textwrap
import threading
from collections.abc import Iterable, Sequence
from pathlib import Path
from typing import Any

import nanobind  # type: ignore

from . import _include, _odext
from ._access import Access
from ._codegen import CodeGenerationContext
from ._cpp_utils import (
    CppExpression,
    CppFunctionScopeDefs,
    CppIdentifier,
    CppNamespaceScopeDefs,
    CppTypeName,
    ModuleCodeFragment,
)
from ._events import EventType
from ._graph import Graph
from ._param import Param


def _exception_types(module_var: CppIdentifier) -> ModuleCodeFragment:
    return ModuleCodeFragment(
        (),
        (),
        CppNamespaceScopeDefs(""),
        CppFunctionScopeDefs(
            f'nanobind::exception<tcspc::end_of_processing>({module_var}, "EndOfProcessing");\n'
        ),
    )


def _context_type(
    accesses: Sequence[tuple[str, type[Access]]],
    module_var: CppIdentifier,
) -> ModuleCodeFragment:
    # We add specific bindings of access() for each access tag so that Python
    # code doesn't need to specify the type of the accessor. We also keep the
    # processor alive (nursed by the accessor) so that the accessor does not
    # dangle.
    return ModuleCodeFragment(
        (),
        (),
        CppNamespaceScopeDefs(""),
        CppFunctionScopeDefs(
            f'nanobind::class_<tcspc::context>({module_var}, "Context")\n'
            + "".join(
                textwrap.indent(
                    textwrap.dedent(f"""\

                        .def("access__{tag}", +[](tcspc::context &self,
                                                processor_type *proc) {{
                            return self.access<{typ.cpp_type_name()}>("{tag}");
                        }}, nanobind::keep_alive<0, 2>())"""),
                    prefix="    ",
                )
                for tag, typ in accesses
            )
            + ";\n"
            + "\n"
            + f'{module_var}.def("create_context", &tcspc::context::create);'
        ),
    )


def _param_struct(
    param_types: Iterable[tuple[CppIdentifier, CppTypeName]],
    module_var: CppIdentifier,
) -> ModuleCodeFragment:
    return ModuleCodeFragment(
        (),
        (),
        CppNamespaceScopeDefs(
            "struct params {\n"
            + "".join(f"    {typ} {name};\n" for name, typ in param_types)
            + "};"
        ),
        CppFunctionScopeDefs(
            f'nanobind::class_<params>({module_var}, "Params")\n'
            "    .def(nanobind::init<>())"
            + "".join(
                f'\n    .def_rw("{name}", &params::{name})'
                for name, type in param_types
            )
            + ";"
        ),
    )


# No-op wrapper to limit event types to the requested ones.
def _input_processor(
    event_types: Iterable[CppTypeName],
) -> ModuleCodeFragment:
    return ModuleCodeFragment(
        (),
        (),
        CppNamespaceScopeDefs(
            textwrap.dedent("""\
            template <typename Downstream> class input_processor {
                Downstream downstream;

            public:
                explicit input_processor(Downstream &&downstream)
                    : downstream(std::move(downstream)) {}
            """)
            + "".join(
                (
                    textwrap.indent(
                        textwrap.dedent(f"""\

                void handle({event_type} const &event) {{
                    downstream.handle(event);
                }}
                """),
                        prefix="    ",
                    )
                    if not event_type.startswith("nanobind::ndarray<")
                    else textwrap.indent(
                        textwrap.dedent(f"""\
                void handle({event_type} const &event) {{
                    auto spn = tcspc::span(event.data(), event.size());
                    downstream.handle(spn);
                }}
                """),
                        prefix="    ",
                    )
                )
                for event_type in event_types
            )
            + textwrap.dedent("""\

                void flush() { downstream.flush(); }
            };""")
        ),
        CppFunctionScopeDefs(""),
    )


def _graph_funcs(
    graph_code: CppExpression,
    gencontext: CodeGenerationContext,
    event_types: Sequence[CppTypeName],
    module_var: CppIdentifier,
) -> ModuleCodeFragment:
    input_proc_code = _input_processor(event_types)
    indented_graph_code = textwrap.indent(
        graph_code, prefix="    " * 4
    ).lstrip()
    return ModuleCodeFragment(
        input_proc_code.includes,
        input_proc_code.sys_includes + ("memory",),
        CppNamespaceScopeDefs(
            input_proc_code.namespace_scope_defs
            + "\n\n"
            + textwrap.dedent(f"""\
            auto create_processor(
                    std::shared_ptr<tcspc::context> {gencontext.context_varname},
                    params const &{gencontext.params_varname}) {{
                return input_processor({indented_graph_code});
            }}

            using processor_type = decltype(create_processor(
                    std::shared_ptr<tcspc::context>(),
                    params()));""")
        ),
        CppFunctionScopeDefs(
            input_proc_code.nanobind_defs
            + "\n"
            + f'nanobind::class_<processor_type>({module_var}, "Processor")\n'
            + (
                '    .def("handle", &processor_type::handle, nanobind::call_guard<nanobind::gil_scoped_release>())'
                if len(event_types) > 0
                else ""
            )
            + '    .def("flush", &processor_type::flush, nanobind::call_guard<nanobind::gil_scoped_release>());\n'
            + "\n"
            + f'{module_var}.def("create_processor", &create_processor);'
        ),
    )


def _module_code(
    module_name: str,
    fragments: Iterable[ModuleCodeFragment],
    module_var: CppIdentifier,
) -> str:
    return "\n".join(
        filter(
            None,
            (
                f"#define NB_DOMAIN {module_name}\n",
                "".join(
                    f'#include "{inc}"\n'
                    for frag in fragments
                    for inc in frag.includes
                ),
                "".join(
                    f"#include <{inc}>\n"
                    for frag in fragments
                    for inc in frag.sys_includes
                ),
                "namespace {",
                textwrap.indent(
                    "\n".join(
                        frag.namespace_scope_defs.rstrip() + "\n"
                        for frag in fragments
                    ),
                    prefix="    ",
                ),
                "} // namespace\n",
                f"NB_MODULE({module_name}, {module_var}) {{",
                textwrap.indent(
                    "\n".join(
                        frag.nanobind_defs.rstrip() + "\n"
                        for frag in fragments
                    ),
                    prefix="    ",
                ).rstrip(),
                "} // NB_MODULE\n",
            ),
        )
    )


def _graph_module_code(
    module_name: str, graph: Graph, input_event_types: Iterable[EventType] = ()
):
    n_in, n_out = len(graph.inputs()), len(graph.outputs())
    if n_in != 1:
        raise ValueError(
            f"graph is not executable (must have exactly 1 input port; found {n_in})"
        )
    if n_out > 0:
        raise ValueError(
            f"graph is not executable (must have no output ports; found {n_out})"
        )

    default_includes = ModuleCodeFragment(
        (
            "libtcspc/tcspc.hpp",
            "nanobind/nanobind.h",
            "nanobind/ndarray.h",
            "nanobind/stl/shared_ptr.h",
            "nanobind/stl/string.h",  # For automatic conversion from str.
        ),
        (),
        CppNamespaceScopeDefs(""),
        CppFunctionScopeDefs(""),
    )

    genctx = CodeGenerationContext(
        CppIdentifier("ctx"), CppIdentifier("params")
    )
    graph_expr = graph.cpp_expression(genctx)

    mod_var = CppIdentifier("mod")

    excs = _exception_types(mod_var)

    params = graph.parameters()
    param_struct = _param_struct(
        tuple((p.name, cpp_type) for p, cpp_type in params), mod_var
    )

    context_code = _context_type(graph.accesses(), mod_var)

    proc_code = _graph_funcs(
        graph_expr,
        genctx,
        tuple(e.cpp_type_name() for e in input_event_types),
        mod_var,
    )

    accessor_types = set(typ for tag, typ in graph.accesses())
    accessors = tuple(typ.cpp_bindings(mod_var) for typ in accessor_types)

    return _module_code(
        module_name,
        (
            default_includes,
            excs,
            param_struct,
            context_code,
            proc_code,
        )
        + accessors,
        mod_var,
    )


class CompiledGraph:
    """
    A compiled processing graph.

    Use `compile_graph()` to obtain an instance. Direct instantiaiton form user
    code is not supported.

    Objects of this type should be treated as immutable.
    """

    def __init__(
        self, mod: Any, params: Iterable[Param], accesses: Iterable[str]
    ) -> None:
        self._mod = mod
        self._params = tuple(params)
        self._accesses = tuple(accesses)

    def parameters(self) -> tuple[Param, ...]:
        return self._params

    def accesses(self) -> tuple[str, ...]:
        return self._accesses


@functools.cache
def _nanobind_dir() -> Path:
    return Path(nanobind.include_dir()).parent


_builder = _odext.Builder(
    cpp_std="c++17",
    include_dirs=(
        _include.libtcspc_include_dir(),
        _nanobind_dir() / "include",
        _nanobind_dir() / "ext/robin_map/include",  # For nanobind lib build.
    ),
    extra_source_files=(_nanobind_dir() / "src/nb_combined.cpp",),
)
_importer = _odext.ExtensionImporter()
_mod_ctr = itertools.count()
_build_lock = threading.Lock()


def compile_graph(
    graph: Graph, input_event_types: Iterable[EventType] = ()
) -> CompiledGraph:
    """
    Compile a processing graph. The result can be used for multiple executions.

    Parameters
    ----------
    graph: Graph
        The processing graph to compile. The graph must have exactly one input
        port and no output ports.
    input_event_types: Iterable[EventType]
        The (Python) event types accepted as input (via `handle()`).

    Returns
    -------
    CompiledGraph
        The compiled graph.
    """

    # Serialize builds, at least for now.
    with _build_lock:
        mod_name = f"libtcspc_graph_{next(_mod_ctr)}"
        code = _graph_module_code(mod_name, graph, input_event_types)
        _builder.set_code(code)
        mod_path = _builder.build()
        mod = _importer.import_module(mod_name, mod_path, ok_to_move=True)
        params = (param for param, typ in graph.parameters())
        accesses = (tag for tag, typ in graph.accesses())
        return CompiledGraph(mod, params, accesses)
