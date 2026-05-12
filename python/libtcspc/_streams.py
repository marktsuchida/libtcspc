# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import abstractmethod
from collections.abc import Sequence
from typing import final

from typing_extensions import override

from ._codegen import _CodeGenerationContext
from ._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _string_type,
    _uint64_type,
)
from ._param import Param, _Parameterized


class InputStream(_Parameterized):
    """Base class for binary input streams used by `ReadBinaryStream`.

    Subclasses select between different byte-source backends, such as
    a file on disk or a memory-resident buffer.

    See Also
    --------
    ReadBinaryStream
        Processor that consumes an `InputStream`.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...


@final
class BinaryFileInputStream(InputStream):
    """Binary input stream that reads from a file on disk.

    Parameters
    ----------
    filename : str or Param[str]
        Path to the file to read.
    start_offset : int or Param[int], keyword-only
        Byte offset within the file at which to begin reading. Must be
        non-negative. Default ``0``.

    Raises
    ------
    ValueError
        If ``start_offset`` is a negative integer, or a `Param` whose
        ``default_value`` is negative.

    Notes
    -----
    The file is opened with unbuffered I/O. Failure to open the file,
    or a file shorter than ``start_offset``, is reported when the
    downstream `ReadBinaryStream` performs its first read.

    See Also
    --------
    ReadBinaryStream
        Processor that consumes this stream.
    :cpp:`tcspc::binary_file_input_stream`
        The underlying C++ input stream.
    """

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
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._filename, Param):
            params.append((self._filename, _string_type))
        if isinstance(self._start_offset, Param):
            params.append((self._start_offset, _uint64_type))
        return params

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        start_offset = gencontext.u64_expression(self._start_offset)
        return _CppExpression(
            f"""\
            tcspc::binary_file_input_stream(
                {gencontext.string_expression(self._filename)},
                tcspc::arg::start_offset<tcspc::u64>{{{start_offset}}}
            )"""
        )
