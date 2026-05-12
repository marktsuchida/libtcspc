# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools
import threading
from collections.abc import Iterable, Sequence
from pathlib import Path
from typing import Any

import nanobind  # type: ignore

from . import _include, _odext
from ._access import AccessTag, _AccessSpec
from ._codegen import _CodeGenerationContext
from ._cpp_utils import (
    _CppExpression,
    _CppFunctionScopeDefs,
    _CppIdentifier,
    _CppNamespaceScopeDefs,
    _CppTypeName,
    _ModuleCodeFragment,
    _quote_string,
)
from ._events import EventType
from ._graph import Graph
from ._param import Param


def _exception_types(module_var: _CppIdentifier) -> _ModuleCodeFragment:
    return _ModuleCodeFragment(
        (),
        (),
        (),
        (
            _CppFunctionScopeDefs(
                f'nanobind::exception<tcspc::end_of_processing>({module_var}, "EndOfProcessing");\n'
            ),
        ),
    )


def _context_type(
    accesses: Sequence[tuple[AccessTag, type[_AccessSpec]]],
    module_var: _CppIdentifier,
) -> _ModuleCodeFragment:
    # We add specific bindings of access() for each access tag so that Python
    # code doesn't need to specify the type of the accessor. We also keep the
    # processor alive (nursed by the accessor) so that the accessor does not
    # dangle.
    return _ModuleCodeFragment(
        (),
        (),
        (),
        (
            _CppFunctionScopeDefs(
                f'nanobind::class_<tcspc::context>({module_var}, "Context", nanobind::is_final())'
                + "".join(
                    f"""
                .def({_quote_string(tag._context_method_name())},
                        +[](tcspc::context &self, processor_type *proc) {{
                    return self.access<{typ._cpp_type_name()}>("{tag.tag}");
                }}, nanobind::keep_alive<0, 2>())"""
                    for tag, typ in accesses
                )
                + ";"
            ),
            _CppFunctionScopeDefs(
                f'{module_var}.def("create_context", &tcspc::context::create);'
            ),
        ),
    )


def _param_struct(
    param_types: Iterable[tuple[_CppIdentifier, _CppTypeName]],
    module_var: _CppIdentifier,
) -> _ModuleCodeFragment:
    return _ModuleCodeFragment(
        (),
        (),
        (
            _CppNamespaceScopeDefs(
                "struct params {\n"
                + "".join(f"    {typ} {name};\n" for name, typ in param_types)
                + "};"
            ),
        ),
        (
            _CppFunctionScopeDefs(
                f'nanobind::class_<params>({module_var}, "Params", nanobind::is_final())\n'
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
    typename: _CppTypeName, event_types: Iterable[EventType]
) -> _ModuleCodeFragment:
    return _ModuleCodeFragment(
        (),
        (),
        (
            _CppNamespaceScopeDefs(
                f"""\
            template <typename Downstream> class {typename} {{
                Downstream downstream;

            public:
                explicit {typename}(Downstream downstream)
                    : downstream(std::move(downstream)) {{}}
            """
                + "\n".join(
                    event_type._cpp_input_handler(_CppIdentifier("downstream"))
                    for event_type in event_types
                )
                + """
                void flush() { downstream.flush(); }
            };"""
            ),
        ),
        (),
    )


def _pysink() -> _ModuleCodeFragment:
    return _ModuleCodeFragment(
        (),
        ("nanobind/trampoline.h",),
        (
            _CppNamespaceScopeDefs("""\
            class py_sink {
            public:
                virtual ~py_sink() = default;
                virtual void handle(nanobind::object const &event) = 0;
                virtual void flush() = 0;
            };"""),
            _CppNamespaceScopeDefs("""\
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
            _CppFunctionScopeDefs("""\
            nanobind::class_<py_sink, py_sink_trampoline>(mod, "PySink")
                .def(nanobind::init<>())
                .def("handle", &py_sink::handle)
                .def("flush", &py_sink::flush);"""),
        ),
    )


def _output_processor(
    typename: _CppTypeName, event_types: Iterable[EventType]
) -> _ModuleCodeFragment:
    return _ModuleCodeFragment(
        (),
        ("memory",),
        (
            _CppNamespaceScopeDefs(
                f"""\
            class {typename} {{
                std::shared_ptr<py_sink> downstream;

            public:
                explicit {typename}(std::shared_ptr<py_sink> downstream) :
                    downstream(std::move(downstream)) {{}}
            """
                + "\n".join(
                    event_type._cpp_output_handlers(
                        _CppIdentifier("downstream")
                    )
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
    graph_code: _CppExpression,
    gencontext: _CodeGenerationContext,
    output_names: Sequence[_CppExpression],
    input_event_types: Sequence[EventType],
    output_event_sets: Sequence[Sequence[EventType]],
    module_var: _CppIdentifier,
) -> _ModuleCodeFragment:
    input_proc_type = _CppTypeName("input_processor")

    assert len(output_names) == len(output_event_sets)
    output_processors = functools.reduce(
        lambda f, g: f + g,
        (
            _output_processor(
                _CppTypeName(f"output_processor_{i}"), output_event_set
            )
            for i, output_event_set in enumerate(output_event_sets)
        ),
        _ModuleCodeFragment((), (), (), ()),
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
        + _ModuleCodeFragment(
            (),
            (
                "array",
                "memory",
            ),
            (
                _CppNamespaceScopeDefs(f"""\
                auto create_processor(
                        std::shared_ptr<tcspc::context> {gencontext.context_varname},
                        params const &{gencontext.params_varname},
                        std::array<std::shared_ptr<py_sink>, {len(output_event_sets)}>
                        {gencontext.sinks_varname}) {{
                    {output_proc_defs}
                    return {input_proc_type}({graph_code.lstrip()});
                }}"""),
                _CppNamespaceScopeDefs(f"""\
                using processor_type = decltype(create_processor(
                        std::shared_ptr<tcspc::context>(),
                        params(),
                        std::array<std::shared_ptr<py_sink>, {len(output_event_sets)}>{{}}));"""),
            ),
            (
                _CppFunctionScopeDefs(
                    f'nanobind::class_<processor_type>({module_var}, "Processor", nanobind::is_final())'
                    + (
                        '\n    .def("handle", &processor_type::handle, nanobind::call_guard<nanobind::gil_scoped_release>())'
                        if len(input_event_types) > 0
                        else ""
                    )
                    + '\n    .def("flush", &processor_type::flush, nanobind::call_guard<nanobind::gil_scoped_release>());\n'
                ),
                _CppFunctionScopeDefs(
                    f'{module_var}.def("create_processor", &create_processor);'
                ),
            ),
        )
    )


def _module_code(
    module_name: str,
    fragments: _ModuleCodeFragment,
    module_var: _CppIdentifier,
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

    output_event_types = graph._map_event_sets((input_event_types,))

    default_includes = _ModuleCodeFragment(
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

    genctx = _CodeGenerationContext(
        _CppIdentifier("ctx"),
        _CppIdentifier("params"),
        _CppIdentifier("sinks"),
    )
    out_proc_names = tuple(
        _CppExpression(f"output_{i}") for i in range(len(output_event_types))
    )
    graph_expr = graph._cpp_expression(genctx, out_proc_names)

    mod_var = _CppIdentifier("mod")

    excs = _exception_types(mod_var)

    params = graph._parameters()
    param_struct = _param_struct(
        tuple((p._cpp_identifier(), cpp_type) for p, cpp_type in params),
        mod_var,
    )

    context_code = _context_type(graph._accesses(), mod_var)

    proc_code = _processor_creation(
        graph_expr,
        genctx,
        out_proc_names,
        input_event_types,
        output_event_types,
        mod_var,
    )

    accessor_types = set(typ for tag, typ in graph._accesses())
    accessors = functools.reduce(
        lambda f, g: f + g,
        (typ.cpp_bindings(mod_var) for typ in accessor_types),
        _ModuleCodeFragment((), (), (), ()),
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


@functools.cache
def _nanobind_dir() -> Path:
    return Path(nanobind.include_dir()).parent


_builder = _odext.Builder(
    cpp_std="c++20",
    include_dirs=(
        _include._libtcspc_include_dir(),
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


def _compile_graph_module(
    graph: Graph, input_event_types: Sequence[EventType]
) -> tuple[Any, tuple[Param, ...], tuple[AccessTag, ...]]:
    # Serialize builds, at least for now.
    with _build_lock:
        mod_name = f"libtcspc_graph_{next(_mod_ctr)}"
        code = _graph_module_code(mod_name, graph, input_event_types)
        _builder.set_code(code)
        mod_path = _builder.build()
        mod = _importer.import_module(mod_name, mod_path, ok_to_move=True)
        params = tuple(param for param, typ in graph._parameters())
        accesses = tuple(tag for tag, typ in graph._accesses())
        return mod, params, accesses


class CompiledGraph:
    """
    A compiled processing graph. The result can be used for multiple executions.

    Objects of this type should be treated as immutable.

    Parameters
    ----------
    graph: Graph
        The processing graph to compile. The graph must have exactly one input
        port and no output ports.
    input_event_types: Iterable[EventType]
        The (Python) event types accepted as input (via `handle()`).
    """

    def __init__(
        self, graph: Graph, input_event_types: Sequence[EventType] = ()
    ) -> None:
        self._mod, self._params, self._access_tags = _compile_graph_module(
            graph, input_event_types
        )

    def parameters(self) -> tuple[Param, ...]:
        return self._params

    def _accesses(self) -> tuple[AccessTag, ...]:
        return self._access_tags
