<!--
This file is part of libtcspc
Copyright 2019-2024 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# C++ coding guidelines

Code should pass the pre-commit hook (including formatting by clang-format).

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

(TODO).
