# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import abstractmethod

from typing_extensions import override

from ._codegen import _CodeGenerationContext
from ._cpp_utils import _CppExpression
from ._numeric_traits import NumericTraits
from ._param import _Parameterized


class DataMapper(_Parameterized):
    """Base class for data mappers used by the `MapToDatapoints` processor.

    A data mapper extracts a scalar datapoint value from each event of the
    mapped type. Concrete subclasses select different event fields.

    See Also
    --------
    :cpp:`tcspc::map_to_datapoints`
        The processor that uses a data mapper.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...


class DifftimeDataMapper(DataMapper):
    """Data mapper that maps the ``difftime`` field to the datapoint value.

    The mapped event must have a ``difftime`` field.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``datapoint_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::difftime_data_mapper`
        The underlying C++ data mapper.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression(
            f"tcspc::difftime_data_mapper<{self._numeric_traits._cpp_type_name()}>()"
        )


class CountDataMapper(DataMapper):
    """Data mapper that maps the ``count`` field to the datapoint value.

    The mapped event must have a ``count`` field.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``datapoint_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::count_data_mapper`
        The underlying C++ data mapper.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression(
            f"tcspc::count_data_mapper<{self._numeric_traits._cpp_type_name()}>()"
        )


class ChannelDataMapper(DataMapper):
    """Data mapper that maps the ``channel`` field to the datapoint value.

    The mapped event must have a ``channel`` field.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``datapoint_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::channel_data_mapper`
        The underlying C++ data mapper.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression(
            f"tcspc::channel_data_mapper<{self._numeric_traits._cpp_type_name()}>()"
        )
