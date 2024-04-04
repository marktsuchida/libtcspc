# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import cppyy


class Access:
    cpp_type: str | None = None

    def __init__(self, cpp_ctx, name: str, ref: object) -> None:
        self._ref = ref
        try:
            self._access = cpp_ctx.access[self.cpp_type](name)
        except cppyy.gbl.std.range_error as e:
            raise LookupError(f"Access for node {name} does not exist") from e
        except cppyy.gbl.std.bad_any_cast as e:
            raise TypeError(
                f"Access for node {name} exists but does not have type {type}"
            ) from e


class CountAccess(Access):
    cpp_type = "tcspc::count_access"

    def count(self) -> int:
        return self._access.count()
