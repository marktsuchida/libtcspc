# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import abstractmethod
from collections.abc import Sequence

from typing_extensions import override

from ._codegen import _CodeGenerationContext
from ._cpp_utils import _CppExpression, _CppTypeName, _size_type
from ._numeric_traits import NumericTraits
from ._param import Param, _Parameterized


class TimingGenerator(_Parameterized):
    """Base class for timing generators used by the `Generate` processor.

    A timing generator produces a pattern of output timing events in response
    to each trigger event. Concrete subclasses generate different patterns.

    See Also
    --------
    :cpp:`tcspc::generate`
        The processor that uses a timing generator.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...


class NullTimingGenerator(TimingGenerator):
    """Timing generator that never generates any output event.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::null_timing_generator`
        The underlying C++ timing generator.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression(
            f"tcspc::null_timing_generator<{self._numeric_traits._cpp_type_name()}>()"
        )


class OneShotTimingGenerator(TimingGenerator):
    """Timing generator that emits a single output event at a fixed delay.

    Parameters
    ----------
    delay : int or Param[int]
        Delay (in abstime units) from the trigger to the generated event.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::one_shot_timing_generator`
        The underlying C++ timing generator.
    """

    def __init__(
        self,
        delay: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._delay = delay
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._delay, Param):
            at = _CppTypeName(
                f"{self._numeric_traits._cpp_type_name()}::abstime_type"
            )
            return ((self._delay, at),)
        return ()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        at = f"{nt}::abstime_type"
        if isinstance(self._delay, Param):
            value = (
                f"{gencontext.params_varname}.{self._delay._cpp_identifier()}"
            )
        else:
            value = str(self._delay)
        return _CppExpression(
            f"tcspc::one_shot_timing_generator<{nt}>("
            f"tcspc::arg::delay<{at}>{{static_cast<{at}>({value})}})"
        )


class DynamicOneShotTimingGenerator(TimingGenerator):
    """Timing generator that emits a single output event whose delay comes from the trigger.

    The trigger event must have a ``delay`` field.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::dynamic_one_shot_timing_generator`
        The underlying C++ timing generator.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression(
            f"tcspc::dynamic_one_shot_timing_generator<{self._numeric_traits._cpp_type_name()}>()"
        )


class LinearTimingGenerator(TimingGenerator):
    """Timing generator that emits a fixed number of equally-spaced output events.

    Parameters
    ----------
    delay : int or Param[int]
        Delay (in abstime units) from the trigger to the first generated
        event.
    interval : int or Param[int]
        Interval (in abstime units) between successive generated events.
    count : int or Param[int]
        Number of events to generate per trigger.
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::linear_timing_generator`
        The underlying C++ timing generator.
    """

    def __init__(
        self,
        delay: int | Param[int],
        interval: int | Param[int],
        count: int | Param[int],
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._delay = delay
        self._interval = interval
        self._count = count
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        at = _CppTypeName(
            f"{self._numeric_traits._cpp_type_name()}::abstime_type"
        )
        params: list[tuple[Param, _CppTypeName]] = []
        if isinstance(self._delay, Param):
            params.append((self._delay, at))
        if isinstance(self._interval, Param):
            params.append((self._interval, at))
        if isinstance(self._count, Param):
            params.append((self._count, _size_type))
        return params

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()
        at = f"{nt}::abstime_type"

        def at_value(v: int | Param[int]) -> str:
            if isinstance(v, Param):
                inner = f"{gencontext.params_varname}.{v._cpp_identifier()}"
            else:
                inner = str(v)
            return f"static_cast<{at}>({inner})"

        count = gencontext.size_t_expression(self._count)
        return _CppExpression(
            f"tcspc::linear_timing_generator<{nt}>("
            f"tcspc::arg::delay<{at}>{{{at_value(self._delay)}}}, "
            f"tcspc::arg::interval<{at}>{{{at_value(self._interval)}}}, "
            f"tcspc::arg::count<std::size_t>{{{count}}})"
        )


class DynamicLinearTimingGenerator(TimingGenerator):
    """Timing generator that emits equally-spaced events with parameters from the trigger.

    The trigger event must have ``delay``, ``interval``, and ``count`` fields.

    Parameters
    ----------
    numeric_traits : NumericTraits or None
        Numeric traits specifying ``abstime_type``. Defaults to
        ``NumericTraits()``.

    See Also
    --------
    :cpp:`tcspc::dynamic_linear_timing_generator`
        The underlying C++ timing generator.
    """

    def __init__(self, numeric_traits: NumericTraits | None = None) -> None:
        self._numeric_traits = numeric_traits or NumericTraits()

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression(
            f"tcspc::dynamic_linear_timing_generator<{self._numeric_traits._cpp_type_name()}>()"
        )
