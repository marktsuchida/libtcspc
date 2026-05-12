# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from dataclasses import dataclass

from . import _cpp_utils
from ._access import AccessTag
from ._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _quote_string,
)
from ._param import Param


@dataclass(frozen=True)
class _CodeGenerationContext:
    context_varname: _CppIdentifier
    params_varname: _CppIdentifier
    sinks_varname: _CppIdentifier

    def u64_expression(self, p: Param[int] | int) -> _CppExpression:
        if isinstance(p, Param):
            return _CppExpression(
                f"{self.params_varname}.{p._cpp_identifier()}"
            )
        if p < 0:
            raise ValueError("non-negative value required")
        return _CppExpression(f"tcspc::u64{{{p}uLL}}")

    def size_t_expression(self, p: Param[int] | int) -> _CppExpression:
        if isinstance(p, Param):
            return _CppExpression(
                f"{self.params_varname}.{p._cpp_identifier()}"
            )
        if p < 0:
            raise ValueError("non-negative value required")
        return _CppExpression(f"std::size_t{{{p}uLL}}")

    def string_expression(self, p: Param[str] | str) -> _CppExpression:
        if isinstance(p, Param):
            return _CppExpression(
                f"{self.params_varname}.{p._cpp_identifier()}"
            )
        return _cpp_utils._quote_string(p)

    def tracker_expression(
        self, access_type: _CppTypeName, access_tag: AccessTag
    ) -> _CppExpression:
        return _CppExpression(
            f"{self.context_varname}->tracker<{access_type}>({_quote_string(access_tag.tag)})"
        )
