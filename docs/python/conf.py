# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import os
from importlib.metadata import version as _v
from pathlib import Path

project = "libtcspc"
author = "Mark A. Tsuchida"
copyright = "2019-2026, Board of Regents of the University of Wisconsin System"

release = _v("libtcspc")
version = release

extensions = [
    "myst_parser",
    "sphinx.ext.autodoc",
    "sphinx.ext.autosummary",
    "sphinx.ext.napoleon",
    "sphinx.ext.intersphinx",
    "sphinx.ext.viewcode",
    "sphinxcontrib.doxylink",
]

source_suffix = {".md": "markdown", ".rst": "restructuredtext"}

myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "fieldlist",
    "smartquotes",
    "substitution",
]

autosummary_generate = True

autodoc_default_options = {
    "members": True,
    "show-inheritance": True,
    "special-members": "__call__",
}

autoclass_content = "both"

autodoc_inherit_docstrings = False

autodoc_typehints = "description"

napoleon_numpy_docstring = True
napoleon_google_docstring = False

intersphinx_mapping = {
    "python": ("https://docs.python.org/3", None),
    "numpy": ("https://numpy.org/doc/stable/", None),
}

doxylink = {
    "cpp": (
        str(
            Path(
                os.environ.get(
                    "LIBTCSPC_DOXYGEN_TAGFILE",
                    "../../build/docs/libtcspc.tag",
                )
            ).resolve()
        ),
        os.environ.get("LIBTCSPC_DOXYGEN_HTML_BASE", "../cpp"),
    ),
}

html_theme = "furo"
html_title = "libtcspc Python"

templates_path = ["_templates"]
html_static_path = ["_static"]
html_css_files = ["custom.css"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

default_role = "py:obj"

nitpicky = False


def _resolve_public_base(base):
    """Return the first public class along ``base``'s MRO, or ``base``
    itself if it is already public or not a class.
    """
    mro = getattr(base, "__mro__", None)
    if mro is None:
        return base
    name = getattr(base, "__name__", "")
    if not name.startswith("_"):
        return base
    for cls in mro[1:]:
        cls_name = getattr(cls, "__name__", "")
        if cls_name and not cls_name.startswith("_"):
            return cls
    return None


def _process_bases(app, name, obj, options, bases):
    resolved = []
    seen_ids = set()
    for base in bases:
        new_base = _resolve_public_base(base)
        if new_base is None:
            continue
        if id(new_base) in seen_ids:
            continue
        seen_ids.add(id(new_base))
        resolved.append(new_base)
    bases[:] = resolved


def setup(app):
    app.connect("autodoc-process-bases", _process_bases)
