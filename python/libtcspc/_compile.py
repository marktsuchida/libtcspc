# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "CompiledGraph",
    "compile_graph",
]

import functools
import itertools
import threading
from collections.abc import Iterable, Sequence
from pathlib import Path
from typing import Any

import nanobind  # type: ignore

from . import _include, _odext
from ._access import Access, AccessTag
from ._codegen import CodeGenerationContext
from ._cpp_utils import (
    CppExpression,
    CppFunctionScopeDefs,
    CppIdentifier,
    CppNamespaceScopeDefs,
    CppTypeName,
    ModuleCodeFragment,
    quote_string,
)
from ._events import EventType
from ._graph import Graph
from ._param import Param


def _exception_types(module_var: CppIdentifier) -> ModuleCodeFragment:
    return ModuleCodeFragment(
        (),
        (),
        (),
        (
            CppFunctionScopeDefs(
                f'nanobind::exception<tcspc::end_of_processing>({module_var}, "EndOfProcessing");\n'
            ),
        ),
    )


def _context_type(
    accesses: Sequence[tuple[AccessTag, type[Access]]],
    module_var: CppIdentifier,
) -> ModuleCodeFragment:
    # We add specific bindings of access() for each access tag so that Python
    # code doesn't need to specify the type of the accessor. We also keep the
    # processor alive (nursed by the accessor) so that the accessor does not
    # dangle.
    return ModuleCodeFragment(
        (),
        (),
        (),
        (
            CppFunctionScopeDefs(
                f'nanobind::class_<tcspc::context>({module_var}, "Context")'
                + "".join(
                    f"""
                .def({quote_string(tag._context_method_name())},
                        +[](tcspc::context &self, processor_type *proc) {{
                    return self.access<{typ.cpp_type_name()}>("{tag.tag}");
                }}, nanobind::keep_alive<0, 2>())"""
                    for tag, typ in accesses
                )
                + ";"
            ),
            CppFunctionScopeDefs(
                f'{module_var}.def("create_context", &tcspc::context::create);'
            ),
        ),
    )


def _param_struct(
    param_types: Iterable[tuple[CppIdentifier, CppTypeName]],
    module_var: CppIdentifier,
) -> ModuleCodeFragment:
    return ModuleCodeFragment(
        (),
        (),
        (
            CppNamespaceScopeDefs(
                "struct params {\n"
                + "".join(f"    {typ} {name};\n" for name, typ in param_types)
                + "};"
            ),
        ),
        (
            CppFunctionScopeDefs(
                f'nanobind::class_<params>({module_var}, "Params")\n'
                "    .def(nanobind::init<>())"
                + "".join(
                    f'\n    .def_rw("{name}", &params::{name})'
                    for name, typ in param_types
                )
                + ";"
            ),
        ),
    )


# No-op wrapper to limit event types to the requested ones.
def _input_processor(
    typename: CppTypeName, event_types: Iterable[EventType]
) -> ModuleCodeFragment:
    return ModuleCodeFragment(
        (),
        (),
        (
            CppNamespaceScopeDefs(
                f"""\
            template <typename Downstream> class {typename} {{
                Downstream downstream;

            public:
                explicit {typename}(Downstream downstream)
                    : downstream(std::move(downstream)) {{}}
            """
                + "\n".join(
                    event_type.cpp_input_handler(CppIdentifier("downstream"))
                    for event_type in event_types
                )
                + """
                void flush() { downstream.flush(); }
            };"""
            ),
        ),
        (),
    )


def _pysink() -> ModuleCodeFragment:
    return ModuleCodeFragment(
        (),
        ("nanobind/trampoline.h",),
        (
            CppNamespaceScopeDefs("""\
            class py_sink {
            public:
                virtual ~py_sink() = default;
                virtual void handle(nanobind::object const &event) = 0;
                virtual void flush() = 0;
            };"""),
            CppNamespaceScopeDefs("""\
            class py_sink_trampoline : public py_sink {
            public:
                NB_TRAMPOLINE(py_sink, 2);
                void handle(nanobind::object const &event) override {
                    NB_OVERRIDE_PURE(handle, event);
                }
                void flush() override { NB_OVERRIDE_PURE(flush); }
            };"""),
        ),
        (
            CppFunctionScopeDefs("""\
            nanobind::class_<py_sink, py_sink_trampoline>(mod, "PySink")
                .def(nanobind::init<>())
                .def("handle", &py_sink::handle)
                .def("flush", &py_sink::flush);"""),
        ),
    )


def _output_processor(
    typename: CppTypeName, event_types: Iterable[EventType]
) -> ModuleCodeFragment:
    return ModuleCodeFragment(
        (),
        ("memory",),
        (
            CppNamespaceScopeDefs(
                f"""\
            class {typename} {{
                std::shared_ptr<py_sink> downstream;

            public:
                explicit {typename}(std::shared_ptr<py_sink> downstream) :
                    downstream(std::move(downstream)) {{}}
            """
                + "\n".join(
                    event_type.cpp_output_handlers(CppIdentifier("downstream"))
                    for event_type in event_types
                )
                + """
                void flush() { downstream->flush(); }
            };"""
            ),
        ),
        (),
    )


def _processor_creation(
    graph_code: CppExpression,
    gencontext: CodeGenerationContext,
    output_names: Sequence[CppExpression],
    input_event_types: Sequence[EventType],
    output_event_sets: Sequence[Sequence[EventType]],
    module_var: CppIdentifier,
) -> ModuleCodeFragment:
    input_proc_type = CppTypeName("input_processor")

    assert len(output_names) == len(output_event_sets)
    output_processors = functools.reduce(
        lambda f, g: f + g,
        (
            _output_processor(
                CppTypeName(f"output_processor_{i}"), output_event_set
            )
            for i, output_event_set in enumerate(output_event_sets)
        ),
        ModuleCodeFragment((), (), (), ()),
    )

    output_proc_defs = "\n".join(
        f"""\
        auto {output_names[i]} = output_processor_{i}(
            {gencontext.sinks_varname}[{i}]);
        """
        for i in range(len(output_names))
    )

    return (
        _pysink()
        + _input_processor(input_proc_type, input_event_types)
        + output_processors
        + ModuleCodeFragment(
            (),
            (
                "array",
                "memory",
            ),
            (
                CppNamespaceScopeDefs(f"""\
                auto create_processor(
                        std::shared_ptr<tcspc::context> {gencontext.context_varname},
                        params const &{gencontext.params_varname},
                        std::array<std::shared_ptr<py_sink>, {len(output_event_sets)}>
                        {gencontext.sinks_varname}) {{
                    {output_proc_defs}
                    return {input_proc_type}({graph_code.lstrip()});
                }}"""),
                CppNamespaceScopeDefs(f"""\
                using processor_type = decltype(create_processor(
                        std::shared_ptr<tcspc::context>(),
                        params(),
                        std::array<std::shared_ptr<py_sink>, {len(output_event_sets)}>{{}}));"""),
            ),
            (
                CppFunctionScopeDefs(
                    f'nanobind::class_<processor_type>({module_var}, "Processor")'
                    + (
                        '\n    .def("handle", &processor_type::handle, nanobind::call_guard<nanobind::gil_scoped_release>())'
                        if len(input_event_types) > 0
                        else ""
                    )
                    + '\n    .def("flush", &processor_type::flush, nanobind::call_guard<nanobind::gil_scoped_release>());\n'
                ),
                CppFunctionScopeDefs(
                    f'{module_var}.def("create_processor", &create_processor);'
                ),
            ),
        )
    )


def _module_code(
    module_name: str,
    fragments: ModuleCodeFragment,
    module_var: CppIdentifier,
) -> str:
    return "\n".join(
        filter(
            None,
            (
                f"#define NB_DOMAIN {module_name}\n",
                "".join(f'#include "{inc}"\n' for inc in fragments.includes),
                "".join(
                    f"#include <{inc}>\n" for inc in fragments.sys_includes
                ),
                "namespace {",
                "\n".join(
                    dfn.strip() + "\n"
                    for dfn in fragments.namespace_scope_defs
                ),
                "} // namespace\n",
                f"NB_MODULE({module_name}, {module_var}) {{",
                "\n".join(
                    dfn.rstrip() + "\n" for dfn in fragments.nanobind_defs
                ).rstrip(),
                "} // NB_MODULE\n",
            ),
        )
    )


def _graph_module_code(
    module_name: str, graph: Graph, input_event_types: Sequence[EventType] = ()
):
    n_in = len(graph.inputs())
    if n_in != 1:
        raise ValueError(
            f"graph is not executable (must have exactly 1 input port; found {n_in})"
        )

    output_event_types = graph.map_event_sets((input_event_types,))

    default_includes = ModuleCodeFragment(
        (
            "libtcspc/tcspc.hpp",
            "nanobind/nanobind.h",
            "nanobind/ndarray.h",
            "nanobind/stl/array.h",
            "nanobind/stl/function.h",
            "nanobind/stl/optional.h",
            "nanobind/stl/shared_ptr.h",
            "nanobind/stl/string.h",
        ),
        (),
        (),
        (),
    )

    genctx = CodeGenerationContext(
        CppIdentifier("ctx"), CppIdentifier("params"), CppIdentifier("sinks")
    )
    out_proc_names = tuple(
        CppExpression(f"output_{i}") for i in range(len(output_event_types))
    )
    graph_expr = graph.cpp_expression(genctx, out_proc_names)

    mod_var = CppIdentifier("mod")

    excs = _exception_types(mod_var)

    params = graph.parameters()
    param_struct = _param_struct(
        tuple((p.cpp_identifier(), cpp_type) for p, cpp_type in params),
        mod_var,
    )

    context_code = _context_type(graph.accesses(), mod_var)

    proc_code = _processor_creation(
        graph_expr,
        genctx,
        out_proc_names,
        input_event_types,
        output_event_types,
        mod_var,
    )

    accessor_types = set(typ for tag, typ in graph.accesses())
    accessors = functools.reduce(
        lambda f, g: f + g,
        (typ.cpp_bindings(mod_var) for typ in accessor_types),
        ModuleCodeFragment((), (), (), ()),
    )

    return _module_code(
        module_name,
        default_includes
        + excs
        + param_struct
        + context_code
        + proc_code
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
        self, mod: Any, params: Iterable[Param], accesses: Iterable[AccessTag]
    ) -> None:
        self._mod = mod
        self._params = tuple(params)
        self._accesses = tuple(accesses)

    def parameters(self) -> tuple[Param, ...]:
        return self._params

    def accesses(self) -> tuple[AccessTag, ...]:
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
    pch_includes=("libtcspc/tcspc.hpp",),
    # Do not include nanobind in pch, because we need to vary NB_DOMAIN.
)
_importer = _odext.ExtensionImporter()
_mod_ctr = itertools.count()
_build_lock = threading.Lock()


def compile_graph(
    graph: Graph, input_event_types: Sequence[EventType] = ()
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
