# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import subprocess
import sys
import textwrap

from libtcspc import _odext

module_code = textwrap.dedent("""\
    #define PY_SSIZE_T_CLEAN
    #include <Python.h>

    namespace {

    PyObject *example_answer(PyObject *module, PyObject *) {
        return PyLong_FromLong(42);
    }

    PyMethodDef example_methods[] = {
        {"answer", example_answer, METH_NOARGS, "Answer the question."},
        {nullptr, nullptr, 0, nullptr}
    };


    struct PyModuleDef example_module = {
        PyModuleDef_HEAD_INIT,
        .m_name = "@odext_module_name@",
        .m_doc = "Test module.",
        .m_size = -1,
        .m_methods = example_methods,
    };

    } // namespace

    PyMODINIT_FUNC PyInit_@odext_module_name@() {
        return PyModule_Create(&example_module);
    }
""")


def test_extension_build_and_import():
    module_name = "odext_test_module_0"
    code = module_code.replace("@odext_module_name@", module_name)
    importer = _odext.ExtensionImporter()
    with _odext.Builder(cpp_std="c++17", code_text=code) as builder:
        mod_file = builder.build()
        mod = importer.import_module(module_name, mod_file, ok_to_move=True)
    assert module_name in sys.modules
    assert mod.answer() == 42


def test_executable_build():
    hello_code = textwrap.dedent("""\
    #include <cstdio>
    int main() { std::printf("hello\\n"); }
    """)
    with _odext.Builder(
        binary_type="executable", code_text=hello_code
    ) as builder:
        exe = builder.build()
        result = subprocess.run(str(exe), stdout=subprocess.PIPE)
    assert result.returncode == 0
    assert result.stdout.decode().rstrip() == "hello"
