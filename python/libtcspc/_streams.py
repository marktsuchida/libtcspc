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
        self, filename: str | Param[str], *, start_offset: int | Param[int] = 0
    ) -> None:
        if isinstance(start_offset, int) and start_offset < 0:
            raise ValueError("start_offset must not be negative")
        if (
            isinstance(start_offset, Param)
            and start_offset.default_value is not None
            and start_offset.default_value < 0
        ):
            raise ValueError(
                "default value for start_offset must not be negative"
            )
        self._filename = filename
        self._start_offset = start_offset

    @override
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        params: list[tuple[Param, CppTypeName]] = []
        if isinstance(self._filename, Param):
            params.append((self._filename, CppTypeName("std::string")))
        if isinstance(self._start_offset, Param):
            params.append((self._start_offset, CppTypeName("tcspc::u64")))
        return params

    @override
    def cpp_expression(
        self, gencontext: CodeGenerationContext
    ) -> CppExpression:
        start_offset = gencontext.u64_expression(self._start_offset)
        return CppExpression(
            dedent(f"""\
                tcspc::binary_file_input_stream(
                    {gencontext.string_expression(self._filename)},
                    tcspc::arg::start_offset<tcspc::u64>{{{start_offset}}}
                )""")
        )
