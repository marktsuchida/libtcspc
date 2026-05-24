# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Sequence
from typing import final

from typing_extensions import override

from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _size_type,
)
from .._events import EventType
from .._node import Node
from .._numeric_traits import NumericTraits
from .._param import Param
from ._common import (
    _check_events_subset_of,
    _make_type_list,
)


def _input_port_names(
    inputs: int | Sequence[str], cls_name: str
) -> tuple[str, ...]:
    if isinstance(inputs, int):
        if inputs < 2:
            raise ValueError(f"{cls_name} requires at least two inputs")
        return tuple(f"input-{i}" for i in range(inputs))
    names = tuple(inputs)
    if len(names) < 2:
        raise ValueError(f"{cls_name} requires at least two inputs")
    if len(set(names)) != len(names):
        raise ValueError(f"{cls_name} input names must be unique")
    return names


@final
class Merge(Node):
    """Merges two sorted input streams into one, ordered by ``abstime``.

    Events arriving on the two inputs are buffered and emitted in
    non-decreasing ``abstime`` order. For events with equal ``abstime``, those
    from ``input-0`` are emitted before those from ``input-1`` (a guaranteed
    tie-breaking order).

    Parameters
    ----------
    *event_types : EventType
        The event types to merge. Every event handled must carry an
        ``abstime`` member; the merge buffers and sorts by it.
    max_buffered : int or Param[int]
        The maximum number of events to buffer from a single input before the
        other input must produce an event. Default: 65536.
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the merge (in particular the
        ``abstime_type``). ``None`` (the default) uses `NumericTraits`
        defaults.

    Notes
    -----
    The node has two input ports, ``"input-0"`` and ``"input-1"``, and a single
    output port. Both inputs must be fed internally (for example by a `Route`
    or `Broadcast` that fans out a single external input); a merge cannot serve
    as the executable graph's external input.

    Events handled:

    - Events matching one of ``event_types``: buffered and emitted in
      non-decreasing ``abstime`` order.
    - Events not in ``event_types``: rejected at graph build time.
    - End of input: flushes any buffered events, then propagates.

    See Also
    --------
    :cpp:`tcspc::merge`
        The underlying C++ factory function.
    :py:obj:`MergeN`
        N-way sorted merge without the equal-``abstime`` tie-breaking
        guarantee.
    :py:obj:`MergeNUnsorted`
        N-way pass-through merge that does no sorting or buffering.
    """

    def __init__(
        self,
        *event_types: EventType,
        max_buffered: int | Param[int] = 65536,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        super().__init__(input=("input-0", "input-1"))
        self._event_types = tuple(event_types)
        self._max_buffered = max_buffered
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 2:
            raise ValueError(
                f"wrong number of inputs (2 expected, {len(input_event_sets)} found)"
            )
        for input_set in input_event_sets:
            _check_events_subset_of(
                input_set, self._event_types, self.__class__.__name__
            )
        return (self._event_types,)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._max_buffered, Param):
            return ((self._max_buffered, _size_type),)
        return ()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        if len(downstreams) != 1:
            raise ValueError(
                f"expected 1 downstream; found {len(downstreams)}"
            )
        type_list = _make_type_list(self._event_types)
        nt = self._numeric_traits._cpp_type_name()
        max_buf = gencontext.size_t_expression(self._max_buffered)
        return _CppExpression(
            f"""\
            tcspc::merge<{type_list}, {nt}>(
                tcspc::arg::max_buffered<std::size_t>{{{max_buf}}}, {downstreams[0]})"""
        )


@final
class MergeN(Node):
    """Merges N sorted input streams into one, ordered by ``abstime``.

    Events arriving on the inputs are buffered and emitted in non-decreasing
    ``abstime`` order. Unlike `Merge`, the relative order of events with equal
    ``abstime`` is **not** guaranteed.

    Parameters
    ----------
    inputs : int or Sequence[str]
        The input ports. An integer ``N`` creates ``N`` ports named
        ``"input-0"`` through ``"input-(N-1)"``. A sequence of names creates
        ports with those exact names. Must specify at least two inputs.
    *event_types : EventType
        The event types to merge. Every event handled must carry an
        ``abstime`` member; the merge buffers and sorts by it.
    max_buffered : int or Param[int]
        The maximum number of events to buffer from a single input before
        another input must produce an event. Default: 65536.
    numeric_traits : NumericTraits or None
        Set of integer types parameterising the merge (in particular the
        ``abstime_type``). ``None`` (the default) uses `NumericTraits`
        defaults.

    Notes
    -----
    All inputs must be fed internally (for example by a `Route` or `Broadcast`
    that fans out a single external input); a merge cannot serve as the
    executable graph's external input.

    Events handled:

    - Events matching one of ``event_types``: buffered and emitted in
      non-decreasing ``abstime`` order.
    - Events not in ``event_types``: rejected at graph build time.
    - End of input: flushes any buffered events, then propagates.

    See Also
    --------
    :cpp:`tcspc::merge_n`
        The underlying C++ factory function.
    :py:obj:`Merge`
        2-way sorted merge that guarantees ``input-0`` precedes ``input-1`` on
        equal ``abstime``.
    :py:obj:`MergeNUnsorted`
        N-way pass-through merge that does no sorting or buffering.
    """

    def __init__(
        self,
        inputs: int | Sequence[str],
        *event_types: EventType,
        max_buffered: int | Param[int] = 65536,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        super().__init__(
            input=_input_port_names(inputs, self.__class__.__name__)
        )
        self._event_types = tuple(event_types)
        self._max_buffered = max_buffered
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        n = len(self.inputs())
        if len(input_event_sets) != n:
            raise ValueError(
                f"wrong number of inputs ({n} expected, {len(input_event_sets)} found)"
            )
        for input_set in input_event_sets:
            _check_events_subset_of(
                input_set, self._event_types, self.__class__.__name__
            )
        return (self._event_types,)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._max_buffered, Param):
            return ((self._max_buffered, _size_type),)
        return ()

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        if len(downstreams) != 1:
            raise ValueError(
                f"expected 1 downstream; found {len(downstreams)}"
            )
        n = len(self.inputs())
        type_list = _make_type_list(self._event_types)
        nt = self._numeric_traits._cpp_type_name()
        max_buf = gencontext.size_t_expression(self._max_buffered)
        return _CppExpression(
            f"""\
            tcspc::merge_n<{n}, {type_list}, {nt}>(
                tcspc::arg::max_buffered<std::size_t>{{{max_buf}}}, {downstreams[0]})"""
        )


@final
class MergeNUnsorted(Node):
    """Merges N input streams by passing events through in arrival order.

    Events arriving on any input are emitted immediately, in the order they
    arrive, with no sorting and no buffering. Use this when the inputs are not
    individually sorted, or when ordering does not matter.

    Parameters
    ----------
    inputs : int or Sequence[str]
        The input ports. An integer ``N`` creates ``N`` ports named
        ``"input-0"`` through ``"input-(N-1)"``. A sequence of names creates
        ports with those exact names. Must specify at least two inputs.

    Notes
    -----
    All inputs must be fed internally (for example by a `Route` or `Broadcast`
    that fans out a single external input); a merge cannot serve as the
    executable graph's external input.

    Events handled:

    - All event types: passed through in arrival order.
    - End of input: propagated once all inputs have ended.

    See Also
    --------
    :cpp:`tcspc::merge_n_unsorted`
        The underlying C++ factory function.
    :py:obj:`Merge`
        2-way sorted merge ordered by ``abstime``.
    :py:obj:`MergeN`
        N-way sorted merge ordered by ``abstime``.
    """

    def __init__(self, inputs: int | Sequence[str]) -> None:
        super().__init__(
            input=_input_port_names(inputs, self.__class__.__name__)
        )

    @override
    def _map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        n = len(self.inputs())
        if len(input_event_sets) != n:
            raise ValueError(
                f"wrong number of inputs ({n} expected, {len(input_event_sets)} found)"
            )
        seen: dict[str, EventType] = {}
        for input_set in input_event_sets:
            for t in input_set:
                seen.setdefault(str(t._cpp_type_name()), t)
        return (tuple(seen.values()),)

    @override
    def _cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstreams: Sequence[_CppExpression],
    ) -> _CppExpression:
        if len(downstreams) != 1:
            raise ValueError(
                f"expected 1 downstream; found {len(downstreams)}"
            )
        n = len(self.inputs())
        return _CppExpression(
            f"tcspc::merge_n_unsorted<{n}>({downstreams[0]})"
        )
