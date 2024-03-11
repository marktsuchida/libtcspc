# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import itertools

import cppyy

_cpp_ns_counter = itertools.count()


def isolated_cppdef(code: str):
    # Run C++ code in a unique namespace and return the cppyy namespace object.
    ns = f"cppyy_test_{next(_cpp_ns_counter)}"
    code = f"""namespace tcspc::{ns} {{
{code}
}}"""
    cppyy.cppdef(code)
    return getattr(cppyy.gbl.tcspc, ns)
