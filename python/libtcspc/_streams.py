# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence
from textwrap import dedent
from typing import final

from typing_extensions import override

from ._codegen import CodeGenerationContext
from ._cpp_utils import CppExpression, CppTypeName
from ._param import Param, Parameterized


class InputStream(Parameterized):
    def cpp_expression(
        self, gencontext: CodeGenerationContext
    ) -> CppExpression:
        raise NotImplementedError()


@final
class BinaryFileInputStream(InputStream):
    def __init__(
        self, filename: str | Param[str], *, start: int | Param[int] = 0
    ) -> None:
        self._filename = filename
        self._start = start

    @override
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        params: list[tuple[Param, CppTypeName]] = []
        if isinstance(self._filename, Param):
            params.append((self._filename, CppTypeName("std::string")))
        if isinstance(self._start, Param):
            params.append((self._start, CppTypeName("tcspc::u64")))
        return params

    @override
    def cpp_expression(
        self, gencontext: CodeGenerationContext
    ) -> CppExpression:
        start = gencontext.u64_expression(self._start)
        return CppExpression(
            dedent(f"""\
                tcspc::binary_file_input_stream(
                    {gencontext.string_expression(self._filename)},
                    tcspc::arg::start_offset<tcspc::u64>{{{start}}}
                )""")
        )
