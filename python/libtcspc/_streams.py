# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from textwrap import dedent
from typing import final

from typing_extensions import override

from . import _cpp_utils


class InputStream:
    @property
    def cpp(self) -> str:
        raise NotImplementedError()


@final
class BinaryFileInputStream(InputStream):
    def __init__(self, filename: str, *, start: int = 0) -> None:
        self._filename = filename
        self._start = start

    @override
    @property
    def cpp(self) -> str:
        fn = _cpp_utils.quote_string(self._filename)
        return dedent(f"""\
            tcspc::binary_file_input_stream(
                {fn},
                tcspc::arg::start_offset<tcspc::u64>{{{self._start}}}
            )""")
