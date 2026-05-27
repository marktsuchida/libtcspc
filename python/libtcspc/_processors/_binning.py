# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Collection, Mapping, Sequence
from typing import Any, final

from typing_extensions import override

from .. import _events
from .._access import AccessTag, _AccessorSpec
from .._bin_mappers import BinMapper
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
)
from .._data_mappers import DataMapper
from .._events import EventType
from .._node import _RelayNode
from .._numeric_traits import NumericTraits
from .._param import Param
from ._common import (
    _remove_events_from_set,
    _with_event_added,
)


@final
class ClusterBinIncrements(_RelayNode):
    """Processor that collects bin increments between start and stop events into clusters.

    Bin increments received while within a cluster (after a ``start_event_type``
    and before a ``stop_event_type``) are collected; each completed cluster is
    emitted as a `BinIncrementClusterEvent`.

    Parameters
    ----------
    start_event_type : EventType
        Event type that starts a cluster.
    stop_event_type : EventType
        Event type that ends a cluster.
    numeric_traits : NumericTraits or None
        Numeric traits for the events. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - ``start_event_type``: begin a new cluster (consumed).
    - ``stop_event_type``: emit the current cluster as a
      `BinIncrementClusterEvent` (consumed).
    - `BinIncrementEvent`: recorded into the current cluster (consumed).
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::cluster_bin_increments`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        start_event_type: EventType,
        stop_event_type: EventType,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._start = start_event_type
        self._stop = stop_event_type
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        bi = _events.BinIncrementEvent(self._numeric_traits)
        cluster = _events.BinIncrementClusterEvent(self._numeric_traits)
        out = _remove_events_from_set(
            input_event_set, (bi, self._start, self._stop)
        )
        return _with_event_added(out, cluster)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::cluster_bin_increments<
                {self._start._cpp_type_name()},
                {self._stop._cpp_type_name()},
                {self._numeric_traits._cpp_type_name()}
            >(
                {downstream}
            )"""
        )


@final
class MapToBins(_RelayNode):
    """Processor that maps datapoint events to bin increment events.

    Each `DatapointEvent` is mapped to a `BinIncrementEvent` by the bin mapper
    (or discarded if the bin mapper rejects it); other events pass through.

    Parameters
    ----------
    bin_mapper : BinMapper
        The bin mapper mapping datapoints to bin indices.
    numeric_traits : NumericTraits or None
        Numeric traits for the events. Defaults to ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - `DatapointEvent`: emit a `BinIncrementEvent` (unless discarded).
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::map_to_bins`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        bin_mapper: BinMapper,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._bin_mapper = bin_mapper
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _accesses(self) -> Sequence[tuple[AccessTag, _AccessorSpec]]:
        return self._bin_mapper._accesses()

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._bin_mapper._parameters()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._bin_mapper._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        dp = _events.DatapointEvent(self._numeric_traits)
        bi = _events.BinIncrementEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (dp,))
        return _with_event_added(out, bi)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        bin_mapper = self._bin_mapper._cpp_expression(gencontext)
        return _CppExpression(
            f"""\
            tcspc::map_to_bins<{self._numeric_traits._cpp_type_name()}>(
                {bin_mapper},
                {downstream}
            )"""
        )


@final
class MapToDatapoints(_RelayNode):
    """Processor that maps events to datapoint events using a data mapper.

    Each event of ``event_type`` is mapped to a `DatapointEvent` by the data
    mapper; other events pass through.

    Parameters
    ----------
    event_type : EventType
        The event type to map.
    data_mapper : DataMapper
        The data mapper extracting the datapoint value.
    numeric_traits : NumericTraits or None
        Numeric traits for the emitted `DatapointEvent`. Defaults to
        ``NumericTraits()``.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: emit a `DatapointEvent`.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::map_to_datapoints`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_type: EventType,
        data_mapper: DataMapper,
        numeric_traits: NumericTraits | None = None,
    ) -> None:
        self._event_type = event_type
        self._data_mapper = data_mapper
        self._numeric_traits = (
            numeric_traits if numeric_traits is not None else NumericTraits()
        )

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        return self._data_mapper._parameters()

    @override
    def _param_encoders(self) -> Mapping[str, Callable[[Any], Any]]:
        return self._data_mapper._param_encoders()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        dp = _events.DatapointEvent(self._numeric_traits)
        out = _remove_events_from_set(input_event_set, (self._event_type,))
        return _with_event_added(out, dp)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        mapper = self._data_mapper._cpp_expression(gencontext)
        return _CppExpression(
            f"""\
            tcspc::map_to_datapoints<
                {self._event_type._cpp_type_name()}, {self._numeric_traits._cpp_type_name()}
            >(
                {mapper},
                {downstream}
            )"""
        )
