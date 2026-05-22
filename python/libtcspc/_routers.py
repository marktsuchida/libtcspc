# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import abstractmethod
from collections.abc import Callable, Mapping, Sequence
from typing import Any

from typing_extensions import override

from ._codegen import _CodeGenerationContext
from ._cpp_utils import _CppExpression, _CppTypeName
from ._numeric_traits import NumericTraits
from ._param import Param, _Parameterized


class Router(_Parameterized):
    """Base class for routers used by the `Route` processor.

    A router maps an event to the index of the output port to which it should
    be routed (or to "no output", discarding the event). Concrete subclasses
    select events by different criteria.

    See Also
    --------
    :cpp:`tcspc::route`
        The processor that uses a router.
    """

    @abstractmethod
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression: ...


class NullRouter(Router):
    """Router that discards every routed event.

    Notes
    -----
    Useful as a placeholder, or together with broadcast event types when no
    events need to be routed.

    See Also
    --------
    :cpp:`tcspc::null_router`
        The underlying C++ router.
    """

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        return _CppExpression("tcspc::null_router()")


class ChannelRouter(Router):
    """Router that routes events by their channel number.

    Each routed event's ``channel`` member is looked up in a channel-to-index
    map; the event is routed to the corresponding output port, or discarded if
    the channel is absent from the map.

    Parameters
    ----------
    channel_indices : Mapping[int, int] or Param
        Maps channel number to output-port index. A plain mapping is baked into
        the generated code at compile time. A `Param` binds the whole mapping
        at execution time; ``arity`` must then be given. Must be non-empty.
    arity : int or None, keyword-only
        Number of channel entries N (the compile-time ``channel_router<N, ...>``
        size). Required if and only if ``channel_indices`` is a `Param`; for a
        literal mapping it is inferred and, if given, must equal
        ``len(channel_indices)``.
    numeric_traits : NumericTraits or None, keyword-only
        Provides the C++ ``channel_type`` of the channel numbers. Default:
        ``NumericTraits()``.

    Raises
    ------
    ValueError
        If a literal ``channel_indices`` is empty, contains a negative output
        index, or has a length not matching a given ``arity``; or if
        ``channel_indices`` is a `Param` and ``arity`` is not given.

    See Also
    --------
    :cpp:`tcspc::channel_router`
        The underlying C++ router.
    :py:obj:`NullRouter`
        Router that discards every event.
    """

    def __init__(
        self,
        channel_indices: Mapping[int, int] | Param[Mapping[int, int]],
        *,
        arity: int | None = None,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._numeric_traits = numeric_traits or NumericTraits()
        if isinstance(channel_indices, Param):
            if arity is None:
                raise ValueError(
                    "arity is required when channel_indices is a Param"
                )
            self._ci: Mapping[int, int] | Param[Mapping[int, int]] = (
                channel_indices
            )
            self._n = arity
        else:
            mapping = dict(channel_indices)
            if len(mapping) == 0:
                raise ValueError(
                    "ChannelRouter requires at least one channel mapping"
                )
            if arity is not None and arity != len(mapping):
                raise ValueError("arity does not match channel_indices length")
            for idx in mapping.values():
                if idx < 0:
                    raise ValueError("output index must not be negative")
            self._ci = mapping
            self._n = len(mapping)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._ci, Param):
            ct = f"{self._numeric_traits._cpp_type_name()}::channel_type"
            arr = _CppTypeName(
                f"std::array<std::pair<{ct}, std::size_t>, {self._n}>"
            )
            return ((self._ci, arr),)
        return ()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        if isinstance(self._ci, Param):
            return {
                self._ci.name: lambda m: [
                    (int(c), int(i)) for c, i in dict(m).items()
                ]
            }
        return {}

    @override
    def _cpp_expression(
        self, gencontext: _CodeGenerationContext
    ) -> _CppExpression:
        nt = self._numeric_traits._cpp_type_name()  # Registers custom traits.
        ct = f"{nt}::channel_type"
        if isinstance(self._ci, Param):
            arg = f"{gencontext.params_varname}.{self._ci._cpp_identifier()}"
        else:
            pairs = ", ".join(
                f"std::pair{{static_cast<{ct}>({c}), "
                f"{gencontext.size_t_expression(i)}}}"
                for c, i in self._ci.items()
            )
            arg = f"std::array{{{pairs}}}"
        return _CppExpression(f"tcspc::channel_router<{self._n}, {nt}>({arg})")
