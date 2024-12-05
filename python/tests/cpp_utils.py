# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import subprocess

from libtcspc import _include, _odext

_builder = _odext.Builder(
    binary_type="executable",
    cpp_std="c++17",
    include_dirs=(_include.libtcspc_include_dir(),),
)


def run_cpp_prog(code: str) -> int:
    _builder.set_code(code)
    exe_path = _builder.build()
    result = subprocess.run(exe_path)
    return result.returncode
