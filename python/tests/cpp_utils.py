# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import itertools
from textwrap import dedent

import cppyy

_cpp_name_counter = itertools.count()


def isolated_cppdef(code: str):
    # Run C++ code in a unique namespace and return the cppyy namespace object.
    ns = f"ns{next(_cpp_name_counter)}"
    code = dedent(f"""\
        namespace tcspc::py::isolated::{ns} {{
            {code}
        }}""")
    cppyy.cppdef(code)
    return getattr(cppyy.gbl.tcspc.py.isolated, ns)
