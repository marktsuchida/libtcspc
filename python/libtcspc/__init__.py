# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

# ruff: noqa: E402, F403

import importlib.resources

import cppyy

from ._version import __version__ as __version__

# In order to get the include directory for our C++ headers, we assume that the
# headers are real files that can be located relative to tcspc.hpp. This will
# not work with unusual import methods such as zipimport, but neither do
# extension modules (including cppyy's backend), so nothing is really lost.
# This does work with meson-python's editable install.

# libtcspc headers.
with importlib.resources.as_file(
    importlib.resources.files("libtcspc").joinpath(
        "include/libtcspc/tcspc.hpp"
    )
) as main_header:
    cppyy.add_include_path(str(main_header.parent.parent))

# Additional headers used by Python bindings.
with importlib.resources.as_file(
    importlib.resources.files("libtcspc").joinpath(
        "include/libtcspc_py/handle_span.hpp"
    )
) as py_header:
    cppyy.add_include_path(str(py_header.parent.parent))

cppyy.include("libtcspc/tcspc.hpp")

from ._access import *
from ._bucket_sources import *
from ._compile import *
from ._events import *
from ._execute import *
from ._graph import *
from ._node import *
from ._processors import *
from ._streams import *
