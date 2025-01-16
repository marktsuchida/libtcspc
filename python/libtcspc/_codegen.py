# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "CodeGenerationContext",
]

from dataclasses import dataclass

from . import _cpp_utils
from ._access import AccessTag
from ._cpp_utils import CppExpression, CppIdentifier, CppTypeName, quote_string
from ._param import Param


@dataclass(frozen=True)
class CodeGenerationContext:
    context_varname: CppIdentifier
    params_varname: CppIdentifier
    sinks_varname: CppIdentifier

    def u64_expression(self, p: Param[int] | int) -> CppExpression:
        if isinstance(p, Param):
            return CppExpression(f"{self.params_varname}.{p.cpp_identifier()}")
        if p < 0:
            raise ValueError("non-negative value required")
        return CppExpression(f"tcspc::u64{{{p}uLL}}")

    def size_t_expression(self, p: Param[int] | int) -> CppExpression:
        if isinstance(p, Param):
            return CppExpression(f"{self.params_varname}.{p.cpp_identifier()}")
        if p < 0:
            raise ValueError("non-negative value required")
        return CppExpression(f"std::size_t{{{p}uLL}}")

    def string_expression(self, p: Param[str] | str) -> CppExpression:
        if isinstance(p, Param):
            return CppExpression(f"{self.params_varname}.{p.cpp_identifier()}")
        return _cpp_utils.quote_string(p)

    def tracker_expression(
        self, access_type: CppTypeName, access_tag: AccessTag
    ) -> CppExpression:
        return CppExpression(
            f"{self.context_varname}->tracker<{access_type}>({quote_string(access_tag.tag)})"
        )
