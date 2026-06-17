# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import abstractmethod
from collections.abc import Sequence
from typing import final

from typing_extensions import override

from ._codegen import _CodeGenerationContext
from ._cpp_utils import (
    _bool_type,
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
    :py:obj:`ReadBinaryStream`
        Processor that consumes an `InputStream`.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...


@final
class NullInputStream(InputStream):
    """Binary input stream that contains no data.

    Reading always behaves as if the end of the stream has been reached
    immediately.

    See Also
    --------
    :cpp:`tcspc::null_input_stream`
        The underlying C++ input stream.
    :py:obj:`ReadBinaryStream`
        Processor that consumes this stream.
    """

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression("tcspc::null_input_stream()")


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
    :cpp:`tcspc::binary_file_input_stream`
        The underlying C++ input stream.
    :py:obj:`ReadBinaryStream`
        Processor that consumes this stream.
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


class OutputStream(_Parameterized):
    """Base class for binary output streams used by `WriteBinaryStream`.

    Subclasses select between different byte-sink backends, such as a file
    on disk or a discarding sink.

    See Also
    --------
    :py:obj:`WriteBinaryStream`
        Processor that consumes an `OutputStream`.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...


@final
class NullOutputStream(OutputStream):
    """Binary output stream that discards all data written to it.

    See Also
    --------
    :cpp:`tcspc::null_output_stream`
        The underlying C++ output stream.
    :py:obj:`WriteBinaryStream`
        Processor that consumes this stream.
    """

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression("tcspc::null_output_stream()")


@final
class BinaryFileOutputStream(OutputStream):
    """Binary output stream that writes to a file on disk.

    Parameters
    ----------
    filename : str or Param[str]
        Path to the file to write.
    truncate : bool or Param[bool], keyword-only
        If true, truncate the file if it already exists. Default ``False``.
    append : bool or Param[bool], keyword-only
        If true, append to the file if it already exists. Default ``False``.

    Notes
    -----
    The file is opened with unbuffered I/O. Failure to open the file is
    reported when the upstream `WriteBinaryStream` performs its first write.

    See Also
    --------
    :cpp:`tcspc::binary_file_output_stream`
        The underlying C++ output stream.
    :py:obj:`WriteBinaryStream`
        Processor that consumes this stream.
    """

    def __init__(
        self,
        filename: str | Param[str],
        *,
        truncate: bool | Param[bool] = False,
        append: bool | Param[bool] = False,
    ) -> None:
        self._filename = filename
        self._truncate = truncate
        self._append = append

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._filename, Param):
            params.append((self._filename, _string_type))
        if isinstance(self._truncate, Param):
            params.append((self._truncate, _bool_type))
        if isinstance(self._append, Param):
            params.append((self._append, _bool_type))
        return params

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        truncate = gencontext.bool_expression(self._truncate)
        append = gencontext.bool_expression(self._append)
        return _CppExpression(
            f"""\
            tcspc::binary_file_output_stream(
                {gencontext.string_expression(self._filename)},
                tcspc::arg::truncate<bool>{{{truncate}}},
                tcspc::arg::append<bool>{{{append}}}
            )"""
        )
