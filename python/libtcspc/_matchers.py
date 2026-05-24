# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import abstractmethod
from collections.abc import Sequence

from typing_extensions import override

from ._codegen import _CodeGenerationContext
from ._cpp_utils import _CppExpression, _CppTypeName
from ._numeric_traits import NumericTraits
from ._param import Param, _Parameterized


class Matcher(_Parameterized):
    """Base class for matchers used by the `Match` and `MatchAndConsume` processors.

    A matcher decides, for each event of the matched type, whether it matches.

    See Also
    --------
    :cpp:`tcspc::match`
        A processor that uses a matcher.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...


class AlwaysMatcher(Matcher):
    """Matcher that matches every event.

    See Also
    --------
    :cpp:`tcspc::always_matcher`
        The underlying C++ matcher.
    """

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression("tcspc::always_matcher()")


class NeverMatcher(Matcher):
    """Matcher that matches no event.

    See Also
    --------
    :cpp:`tcspc::never_matcher`
        The underlying C++ matcher.
    """

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression("tcspc::never_matcher()")


class ChannelMatcher(Matcher):
    """Matcher that matches events whose ``channel`` equals a given value.

    Parameters
    ----------
    channel : int or Param[int]
        The channel number to match.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``channel_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::channel_matcher`
        The underlying C++ matcher.
    """

    def __init__(
        self,
        channel: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._channel = channel
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._channel, Param):
            ct = _CppTypeName(
                f"{self._numeric_traits._cpp_type_name()}::channel_type"
            )
            return ((self._channel, ct),)
        return ()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        ct = f"{nt}::channel_type"
        if isinstance(self._channel, Param):
            value = f"{gencontext.params_varname}.{self._channel._cpp_identifier()}"
        else:
            value = str(self._channel)
        return _CppExpression(
            f"tcspc::channel_matcher<{nt}>("
            f"tcspc::arg::channel<{ct}>{{static_cast<{ct}>({value})}})"
        )
