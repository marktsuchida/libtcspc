# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "libtcspc_include_dir",
]

import functools
import importlib.resources
from pathlib import Path


# In order to get the include directory for our C++ headers, we assume that the
# headers are real files that can be located relative to tcspc.hpp. This will
# not work with unusual import methods such as zipimport, but neither do
# extension modules, so nothing is really lost. This does work with
# meson-python's editable install.
@functools.cache
def libtcspc_include_dir() -> Path:
    with importlib.resources.as_file(
        importlib.resources.files("libtcspc").joinpath(
            "include/libtcspc/tcspc.hpp"
        )
    ) as main_header:
        return main_header.parent.parent
