<!--
This file is part of libtcspc
Copyright 2019-2026 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# C++ coding guidelines

Code should pass the pre-commit hooks (including formatting by clang-format),
run with [prek](https://github.com/j178/prek) (`prek install` to set up the git
hook).

Code should have no build warnings (on supported platform-compiler
combinations) and no warnings from clang-tidy.

Suppressing clang-tidy warnings (with `NOLINT` comments) is allowed if
well-considered. The reason for suppression should be left as a comment if not
obvious from the context.

For things that are not mechanically detected or corrected, follow the existing
convention (for example, names are in `snake_case` except for template
parameters, which are in `PascalCase`).

All new code should be accompanied with good unit tests. API functions and
types should be documented with Doxygen comments (follow existing practice).

# Python coding guidelines

Public API symbols should carry
[NumPy-style](https://numpydoc.readthedocs.io/en/latest/format.html#docstring-standard)
docstrings; these are rendered into the Sphinx site under `docs/python/`. Build
the Python docs locally with
`uv run --group docs meson setup <builddir> -Dpython_docs=enabled` followed by
`uv run --group docs ninja -C <builddir> docs/python`.

Run the Python tests with `uv run --group testing pytest`. To test with a
specific Python version, use
`uv run --isolated --python 3.12 --group testing pytest`.
