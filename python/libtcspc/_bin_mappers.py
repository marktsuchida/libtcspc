# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import abstractmethod
from collections.abc import Sequence

from typing_extensions import override

from ._access import AccessTag, _AccessSpec, _UniqueBinMapperAccessSpec
from ._codegen import _CodeGenerationContext
from ._cpp_utils import _CppExpression, _CppTypeName
from ._numeric_traits import NumericTraits
from ._param import Param, _Parameterized


class BinMapper(_Parameterized):
    """Base class for bin mappers used by the `MapToBins` processor.

    A bin mapper maps each datapoint value to a histogram bin index (or
    discards it). Concrete subclasses implement different mappings.

    See Also
    --------
    :cpp:`tcspc::map_to_bins`
        The processor that uses a bin mapper.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...

    def _accesses(self) -> Sequence[tuple[AccessTag, type[_AccessSpec]]]:
        return ()


class PowerOf2BinMapper(BinMapper):
    """Bin mapper that discards the least significant bits of the datapoint.

    Maps a datapoint of ``n_data_bits`` significant bits to a bin index of
    ``n_histo_bits`` bits by right-shifting.

    Parameters
    ----------
    n_data_bits : int
        Number of significant bits in the datapoint.
    n_histo_bits : int
        Number of bits in the bin index (the histogram has
        ``2 ** n_histo_bits`` bins).
    flip : bool
        If ``True``, reverse the bin order. Default ``False``.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``datapoint_type`` and ``bin_index_type``.
        Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::power_of_2_bin_mapper`
        The underlying C++ bin mapper.
    """

    def __init__(
        self,
        n_data_bits: int,
        n_histo_bits: int,
        flip: bool = False,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._n_data_bits = n_data_bits
        self._n_histo_bits = n_histo_bits
        self._flip = flip
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        flip = "true" if self._flip else "false"
        return _CppExpression(
            f"tcspc::power_of_2_bin_mapper<{self._n_data_bits}, "
            f"{self._n_histo_bits}, {flip}, {nt}>()"
        )


class LinearBinMapper(BinMapper):
    """Bin mapper that maps datapoints to bins of equal width.

    Parameters
    ----------
    offset : int or Param[int]
        Datapoint value mapped to the start of bin 0.
    bin_width : int or Param[int]
        Width of each bin in datapoint units. May be negative to reverse the
        mapping.
    max_bin_index : int or Param[int]
        Largest bin index (the histogram has ``max_bin_index + 1`` bins).
    clamp : bool
        If ``True``, datapoints outside the range are clamped to the first or
        last bin instead of discarded. Default ``False``.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``datapoint_type`` and ``bin_index_type``.
        Defaults to ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::linear_bin_mapper`
        The underlying C++ bin mapper.
    """

    def __init__(
        self,
        offset: int | Param[int],
        bin_width: int | Param[int],
        max_bin_index: int | Param[int],
        clamp: bool = False,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._offset = offset
        self._bin_width = bin_width
        self._max_bin_index = max_bin_index
        self._clamp = clamp
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        nt = self._numeric_traits._cpp_type_name()
        dpt = _CppTypeName(f"{nt}::datapoint_type")
        bit = _CppTypeName(f"{nt}::bin_index_type")
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._offset, Param):
            params.append((self._offset, dpt))
        if isinstance(self._bin_width, Param):
            params.append((self._bin_width, dpt))
        if isinstance(self._max_bin_index, Param):
            params.append((self._max_bin_index, bit))
        return params

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        dpt = f"{nt}::datapoint_type"
        bit = f"{nt}::bin_index_type"

        def value(v: int | Param[int]) -> str:
            if isinstance(v, Param):
                return f"{gencontext.params_varname}.{v._cpp_identifier()}"
            return str(v)

        clamp = "true" if self._clamp else "false"
        return _CppExpression(
            f"tcspc::linear_bin_mapper<{nt}>("
            f"tcspc::arg::offset<{dpt}>{{static_cast<{dpt}>({value(self._offset)})}}, "
            f"tcspc::arg::bin_width<{dpt}>{{static_cast<{dpt}>({value(self._bin_width)})}}, "
            f"tcspc::arg::max_bin_index<{bit}>{{static_cast<{bit}>({value(self._max_bin_index)})}}, "
            f"tcspc::arg::clamp{{{clamp}}})"
        )


class UniqueBinMapper(BinMapper):
    """Bin mapper that assigns a new bin index to each distinct datapoint.

    Each datapoint value encountered is assigned the next available bin index,
    so that distinct values map to distinct bins. The mapping (the datapoint
    value assigned to each bin index) can be retrieved at run time via the
    `UniqueBinMapperAccess` obtained from the `ExecutionContext` using
    ``access_tag``.

    Parameters
    ----------
    access_tag : AccessTag
        Tag used to retrieve a `UniqueBinMapperAccess` from the
        `ExecutionContext` at run time.
    max_bin_index : int or Param[int]
        Largest allowed bin index (datapoints beyond this are discarded).
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``datapoint_type`` and ``bin_index_type``.
        Defaults to ``NumericTraits()``. The run-time access requires the
        default ``datapoint_type``.

    See Also
    --------
    :cpp:`tcspc::unique_bin_mapper`
        The underlying C++ bin mapper.
    """

    def __init__(
        self,
        access_tag: AccessTag,
        max_bin_index: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._access_tag = access_tag
        self._max_bin_index = max_bin_index
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _accesses(self) -> Sequence[tuple[AccessTag, type[_AccessSpec]]]:
        return ((self._access_tag, _UniqueBinMapperAccessSpec),)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._max_bin_index, Param):
            bit = _CppTypeName(
                f"{self._numeric_traits._cpp_type_name()}::bin_index_type"
            )
            return ((self._max_bin_index, bit),)
        return ()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        bit = f"{nt}::bin_index_type"
        if isinstance(self._max_bin_index, Param):
            value = f"{gencontext.params_varname}.{self._max_bin_index._cpp_identifier()}"
        else:
            value = str(self._max_bin_index)
        tracker = gencontext.tracker_expression(
            _UniqueBinMapperAccessSpec._cpp_type_name(), self._access_tag
        )
        return _CppExpression(
            f"tcspc::unique_bin_mapper<{nt}>({tracker}, "
            f"tcspc::arg::max_bin_index<{bit}>{{static_cast<{bit}>({value})}})"
        )
