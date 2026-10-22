# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import logging
import platform
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from libtcspc import _odext

module_code = """\
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
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "@odext_module_name@",
    .m_doc = "Test module.",
    .m_size = -1,
    .m_methods = example_methods,
};

} // namespace

PyMODINIT_FUNC PyInit_@odext_module_name@() {
    return PyModule_Create(&example_module);
}
"""


def test_extension_build_and_import():
    module_name = "odext_test_module_0"
    code = module_code.replace("@odext_module_name@", module_name)
    importer = _odext.ExtensionImporter()
    with _odext.Builder(cpp_std="c++20", code_text=code) as builder:
        mod_file = builder.build()
        mod = importer.import_module(module_name, mod_file, ok_to_move=True)
    assert module_name in sys.modules
    assert mod.answer() == 42


hello_code = """#include <cstdio>
int main() { std::printf("hello\\n"); }
"""


def test_executable_build():
    with _odext.Builder(
        binary_type="executable", code_text=hello_code
    ) as builder:
        exe = builder.build()
        result = subprocess.run(str(exe), stdout=subprocess.PIPE)
    assert result.returncode == 0
    assert result.stdout.decode().rstrip() == "hello"


def test_ninja_exe_default(monkeypatch):
    monkeypatch.delenv("NINJA", raising=False)
    expected = shutil.which("ninja")
    assert expected is not None
    assert _odext._ninja_exe() == Path(expected)


def test_ninja_exe_honors_NINJA_env(monkeypatch):
    ninja = shutil.which("ninja")
    assert ninja is not None
    monkeypatch.setenv("NINJA", ninja)
    assert _odext._ninja_exe() == Path(ninja)


def test_ninja_exe_invalid_NINJA_env_raises(monkeypatch):
    monkeypatch.setenv("NINJA", "no-such-ninja-exe")
    with pytest.raises(RuntimeError, match="NINJA"):
        _odext._ninja_exe()


@pytest.mark.skipif(
    platform.system() != "Windows", reason="VS environment is Windows-only"
)
def test_build_env_has_vs_environment():
    with _odext.Builder(
        binary_type="executable", code_text=hello_code
    ) as builder:
        builder.configure()
        assert "INCLUDE" in builder._build_env
        assert "LIB" in builder._build_env
        assert "MESON_DEVENV" not in builder._build_env


@pytest.mark.parametrize(
    "value, expected",
    [(None, None), ("", None), ("0", False), ("1", True)],
)
def test_pch_mode(monkeypatch, value, expected):
    if value is None:
        monkeypatch.delenv("LIBTCSPC_PY_USE_PCH", raising=False)
    else:
        monkeypatch.setenv("LIBTCSPC_PY_USE_PCH", value)
    assert _odext._pch_mode() is expected


def test_pch_mode_invalid_warns(monkeypatch, caplog):
    monkeypatch.setenv("LIBTCSPC_PY_USE_PCH", "yes")
    with caplog.at_level(logging.WARNING):
        assert _odext._pch_mode() is None
    assert "LIBTCSPC_PY_USE_PCH" in caplog.text
    assert "yes" in caplog.text


@pytest.mark.parametrize("value", [None, "0", "1"])
def test_executable_build_with_pch_setting(monkeypatch, value):
    if value is None:
        monkeypatch.delenv("LIBTCSPC_PY_USE_PCH", raising=False)
    else:
        monkeypatch.setenv("LIBTCSPC_PY_USE_PCH", value)
    with _odext.Builder(
        binary_type="executable",
        pch_sys_includes=("cstdio",),
        code_text=hello_code,
    ) as builder:
        exe = builder.build()
        result = subprocess.run(str(exe), stdout=subprocess.PIPE)
    assert result.returncode == 0
    assert result.stdout.decode().rstrip() == "hello"
