# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import functools
import itertools
import threading
from collections.abc import Callable, Iterable, Mapping, Sequence
from pathlib import Path
from typing import Any

import nanobind  # type: ignore

from . import _include, _odext
from ._access import AccessTag, _AccessorSpec
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
from ._events import EventType, _collecting_referenced_events, _Field
from ._graph import Graph
from ._numeric_traits import _collecting_referenced_traits
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
            _CppFunctionScopeDefs(
                f'nanobind::exception<tcspc::source_halted>({module_var}, "SourceHalted");\n'
            ),
        ),
    )


def _context_type(
    accesses: Sequence[tuple[AccessTag, _AccessorSpec]],
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
                    return self.access<{spec._cpp_type_name()}>("{tag.tag}");
                }}, nanobind::keep_alive<0, 2>())"""
                    for tag, spec in accesses
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


def _event_wrappers(
    input_event_types: Iterable[EventType],
    output_event_sets: Iterable[Iterable[EventType]],
    extra_value_event_types: Iterable[EventType],
    module_var: _CppIdentifier,
) -> tuple[_ModuleCodeFragment, list[tuple[EventType, str]]]:
    by_cpp: dict[str, EventType] = {}

    def collect(et: EventType) -> None:
        if not et._supports_value():
            return
        key = str(et._cpp_type_name())
        if key in by_cpp:
            return
        by_cpp[key] = et
        # An array event's wrapper exposes its elements as wrappers of the
        # element type, so element wrappers must be bound too.
        for dep in et._value_dependency_event_types():
            collect(dep)

    for et in input_event_types:
        collect(et)
    for eset in output_event_sets:
        for et in eset:
            collect(et)
    for et in extra_value_event_types:
        collect(et)
    struct_defs = tuple(
        d
        for et in by_cpp.values()
        if (d := et._cpp_wrapper_struct_def()) is not None
    )
    class_defs = tuple(
        et._cpp_wrapper_class_def(module_var) for et in by_cpp.values()
    )
    correspondence = [
        (et, str(et._cpp_wrapper_class_name())) for et in by_cpp.values()
    ]
    return (
        _ModuleCodeFragment(
            (), ("algorithm", "array", "stdexcept"), struct_defs, class_defs
        ),
        correspondence,
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

                [[nodiscard]] auto introspect_node() const
                        -> tcspc::processor_info {{
                    return tcspc::processor_info(this, "{typename}");
                }}

                [[nodiscard]] auto introspect_graph() const
                        -> tcspc::processor_graph {{
                    return downstream.introspect_graph().push_entry_point(this);
                }}
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


def _py_bucket_source_support() -> _ModuleCodeFragment:
    return _ModuleCodeFragment(
        (),
        ("cstddef", "memory", "span", "stdexcept", "utility"),
        (
            _CppNamespaceScopeDefs("""\
            struct py_buffer_storage {
                nanobind::object obj;
                explicit py_buffer_storage(nanobind::object o)
                    : obj(std::move(o)) {}
                py_buffer_storage(py_buffer_storage &&) noexcept = default;
                auto operator=(py_buffer_storage &&) noexcept
                    -> py_buffer_storage & = default;
                py_buffer_storage(py_buffer_storage const &) = delete;
                auto operator=(py_buffer_storage const &)
                    -> py_buffer_storage & = delete;
                ~py_buffer_storage() {
                    if (obj.is_valid()) {
                        nanobind::gil_scoped_acquire held;
                        obj.reset();
                    }
                }
            };

            // Copyable wrapper holding a shared_ptr to the (move-only,
            // GIL-safe) py_buffer_storage. Copying it is an atomic refcount
            // bump with no Python C-API access, so it is safe to copy with the
            // GIL released (as shared_view_of does).
            struct py_buffer_shared_storage {
                std::shared_ptr<py_buffer_storage> ref;
            };"""),
            _CppNamespaceScopeDefs("""\
            template <typename T>
            class py_buffer_bucket_source final : public tcspc::bucket_source<T> {
                nanobind::object source; // the PyBucketSource instance

              public:
                explicit py_buffer_bucket_source(nanobind::object src)
                    : source(std::move(src)) {}

                ~py_buffer_bucket_source() override {
                    if (source.is_valid()) {
                        nanobind::gil_scoped_acquire held;
                        source.reset();
                    }
                }

                auto bucket_of_size(std::size_t size) -> tcspc::bucket<T> override {
                    nanobind::gil_scoped_acquire held;
                    nanobind::object obj = source.attr("bucket_of_size")(size);
                    auto arr = nanobind::cast<nanobind::ndarray<
                        T, nanobind::ndim<1>, nanobind::c_contig,
                        nanobind::device::cpu>>(obj);
                    if (arr.size() < size)
                        throw std::runtime_error(
                            "PyBucketSource.bucket_of_size returned a buffer "
                            "smaller than the requested size");
                    auto const spn = std::span<T>(arr.data(), size);
                    return tcspc::bucket<T>(spn, py_buffer_shared_storage{
                        std::make_shared<py_buffer_storage>(std::move(obj))});
                }

                [[nodiscard]] auto supports_shared_views() const noexcept
                    -> bool override {
                    return true;
                }

                [[nodiscard]] auto shared_view_of(tcspc::bucket<T> const &bkt)
                    -> tcspc::bucket<T const> override {
                    auto storage =
                        bkt.template storage<py_buffer_shared_storage>();
                    return tcspc::bucket<T const>(std::span<T const>(bkt),
                                                  std::move(storage));
                }
            };"""),
            _CppNamespaceScopeDefs("""\
            // Prepare a bucket for transfer to Python: share
            // Python-buffer-backed storage (zero-copy; exposed read-only),
            // otherwise deep-copy. Copying py_buffer_shared_storage is a
            // GIL-free shared_ptr refcount bump, so this is safe to call with
            // the GIL released. The const_cast is sound because exposure to
            // Python is read-only.
            template <typename T>
            auto share_or_copy_bucket(tcspc::bucket<T> const &bkt)
                -> tcspc::bucket<T> {
                if (bkt.template check_storage_type<
                        py_buffer_shared_storage>()) {
                    auto storage =
                        bkt.template storage<py_buffer_shared_storage>();
                    auto const spn = std::span<T>(
                        const_cast<T *>(bkt.data()), bkt.size());
                    return tcspc::bucket<T>(spn, std::move(storage));
                }
                return bkt;
            }"""),
        ),
        (),
    )


def _make_owning_bucket_support() -> _ModuleCodeFragment:
    return _ModuleCodeFragment(
        (),
        ("algorithm", "initializer_list"),
        (
            _CppNamespaceScopeDefs("""\
            template <typename T>
            auto make_owning_bucket(std::initializer_list<T> values)
                -> tcspc::bucket<T> {
                auto bkt = tcspc::new_delete_bucket_source<T>::create()
                               ->bucket_of_size(values.size());
                std::copy(values.begin(), values.end(), bkt.begin());
                return bkt;
            }"""),
        ),
        (),
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

                [[nodiscard]] auto introspect_node() const
                        -> tcspc::processor_info {{
                    return tcspc::processor_info(this, "{typename}");
                }}

                [[nodiscard]] auto introspect_graph() const
                        -> tcspc::processor_graph {{
                    return tcspc::processor_graph().push_entry_point(this);
                }}
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
        + _py_bucket_source_support()
        + _make_owning_bucket_support()
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
                    # handle() is an overload set when there are multiple
                    # input event types, so bind each overload explicitly.
                    + "".join(
                        '\n    .def("handle", nanobind::overload_cast<'
                        f"{et._cpp_input_handler_param_type()} const &>"
                        "(&processor_type::handle), "
                        "nanobind::call_guard<nanobind::gil_scoped_release>())"
                        for et in input_event_types
                    )
                    + '\n    .def("flush", &processor_type::flush, nanobind::call_guard<nanobind::gil_scoped_release>())'
                    + '\n    .def("_graphviz", [](processor_type const &self) {'
                    + "\n        return tcspc::graphviz_from_processor_graph(self.introspect_graph());"
                    + "\n    }, nanobind::call_guard<nanobind::gil_scoped_release>());\n"
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

    graph._check_thread_safety()

    default_includes = _ModuleCodeFragment(
        (
            "libtcspc/tcspc.hpp",
            "nanobind/nanobind.h",
            "nanobind/ndarray.h",
            "nanobind/stl/array.h",
            "nanobind/stl/function.h",
            "nanobind/stl/optional.h",
            "nanobind/stl/pair.h",
            "nanobind/stl/shared_ptr.h",
            "nanobind/stl/string.h",
            "nanobind/stl/vector.h",
        ),
        (),
        (),
        (),
    )

    mod_var = _CppIdentifier("mod")

    with (
        _collecting_referenced_traits() as nt_registry,
        _collecting_referenced_events() as event_registry,
    ):
        genctx = _CodeGenerationContext(
            _CppIdentifier("ctx"),
            _CppIdentifier("params"),
            _CppIdentifier("sinks"),
        )
        out_proc_names = tuple(
            _CppExpression(f"output_{i}")
            for i in range(len(output_event_types))
        )
        graph_expr = graph._cpp_expression(genctx, out_proc_names)

        params = graph._parameters()
        param_struct = _param_struct(
            tuple((p._cpp_identifier(), cpp_type) for p, cpp_type in params),
            mod_var,
        )

        context_code = _context_type(graph._accesses(), mod_var)

        wrappers, wrapper_correspondence = _event_wrappers(
            input_event_types,
            output_event_types,
            graph._value_event_types(),
            mod_var,
        )

        proc_code = _processor_creation(
            graph_expr,
            genctx,
            out_proc_names,
            input_event_types,
            output_event_types,
            mod_var,
        )

        accessor_specs = {
            str(spec._cpp_type_name()): spec
            for _tag, spec in graph._accesses()
        }
        accessors = functools.reduce(
            lambda f, g: f + g,
            (spec.cpp_bindings(mod_var) for spec in accessor_specs.values()),
            _ModuleCodeFragment((), (), (), ()),
        )

    excs = _exception_types(mod_var)

    nt_struct_fragment = _ModuleCodeFragment(
        (),
        (),
        tuple(_CppNamespaceScopeDefs(d) for d in nt_registry.values()),
        (),
    )

    # Must follow nt_struct_fragment: an abstime event's member type references
    # the trait struct.
    custom_event_struct_fragment = _ModuleCodeFragment(
        (),
        (),
        tuple(_CppNamespaceScopeDefs(d) for d in event_registry.values()),
        (),
    )

    code = _module_code(
        module_name,
        default_includes
        + nt_struct_fragment
        + custom_event_struct_fragment
        + excs
        + context_code
        # wrappers must precede param_struct and proc_code: the params struct
        # references array-event wrapper structs for array-event params, and
        # the input/output processors in proc_code reference them too.
        + wrappers
        + param_struct
        + proc_code
        + accessors,
        mod_var,
    )
    return code, wrapper_correspondence


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
) -> tuple[
    Any,
    tuple[Param, ...],
    Mapping[str, Callable[[Any], Any]],
    tuple[AccessTag, ...],
    Mapping[str, _AccessorSpec],
    list[tuple[EventType, Any]],
]:
    # Serialize builds, at least for now.
    with _build_lock:
        mod_name = f"libtcspc_graph_{next(_mod_ctr)}"
        code, wrapper_correspondence = _graph_module_code(
            mod_name, graph, input_event_types
        )
        _builder.set_code(code)
        mod_path = _builder.build()
        mod = _importer.import_module(mod_name, mod_path, ok_to_move=True)
        params = tuple(param for param, typ in graph._parameters())
        encoders = graph._param_encoders()
        accesses = tuple(tag for tag, spec in graph._accesses())
        accessor_specs = {tag.tag: spec for tag, spec in graph._accesses()}
        wrappers = [
            (et, getattr(mod, name)) for et, name in wrapper_correspondence
        ]
        return mod, params, encoders, accesses, accessor_specs, wrappers


class CompiledGraph:
    """
    A compiled processing graph. The result can be used for multiple executions.

    Objects of this type should be treated as immutable.

    Parameters
    ----------
    graph : Graph
        The processing graph to compile. The graph must have exactly one input
        port and no output ports.
    input_event_types : Sequence[EventType]
        The (Python) event types accepted as input (via `handle()`).
    """

    def __init__(
        self, graph: Graph, input_event_types: Sequence[EventType] = ()
    ) -> None:
        (
            self._mod,
            self._params,
            self._param_encoders,
            self._access_tags,
            self._accessor_specs,
            wrappers,
        ) = _compile_graph_module(graph, input_event_types)

        # Map wrapper pyclass -> (EventType, field schema), used to convert a
        # delivered wrapper value to an EventInstance on output. The schema lets
        # the conversion recurse through array-event elements.
        self._eventtype_by_wrapper: dict[
            type, tuple[EventType, list[_Field]]
        ] = {
            pyclass: (et, et._field_schema() or []) for et, pyclass in wrappers
        }
        # Map C++ type-name string -> (wrapper pyclass, field schema), used to
        # convert an EventInstance to a wrapper value on input. Includes the
        # element types that input array events depend on (their wrappers are
        # built when converting array elements).
        input_value_cpp_names: set[str] = set()

        def add_input_deps(et: EventType) -> None:
            if not et._supports_value():
                return
            name = str(et._cpp_type_name())
            if name in input_value_cpp_names:
                return
            input_value_cpp_names.add(name)
            for dep in et._value_dependency_event_types():
                add_input_deps(dep)

        for et in input_event_types:
            add_input_deps(et)
        # Event values bound to a parameter at run time (e.g. Prepend/Append
        # with a Param event) are converted EventInstance -> wrapper on the
        # input side, so their wrappers must be available here too.
        for et in graph._value_event_types():
            add_input_deps(et)
        self._wrapper_by_cpp: dict[str, tuple[type, list[_Field]]] = {
            str(et._cpp_type_name()): (pyclass, et._field_schema() or [])
            for et, pyclass in wrappers
            if str(et._cpp_type_name()) in input_value_cpp_names
        }

    def parameters(self) -> tuple[Param, ...]:
        return self._params

    def _accesses(self) -> tuple[AccessTag, ...]:
        return self._access_tags
