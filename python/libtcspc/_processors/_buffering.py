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
from .._node import _RelayNode
from .._param import Param
from ._common import (
    _check_events_subset_of,
)


@final
class ProcessInBatches(_RelayNode):
    """Processor that buffers a single event type into fixed-size batches and re-emits it.

    Collects every ``batch_size`` events of ``event_type`` into a bucket and
    then re-emits them individually downstream. This is useful for decoupling
    upstream and downstream processing, but introduces latency.

    Parameters
    ----------
    event_type : EventType
        The event type to buffer. The input event set must consist only of
        this type.
    batch_size : int or Param[int]
        Number of events to collect in each batch.

    Notes
    -----
    Events handled:

    - Events matching ``event_type``: buffered and re-emitted unchanged.
    - All other event types: rejected at graph build time.
    - End of input: emit any buffered events, then pass through.

    See Also
    --------
    :cpp:`tcspc::process_in_batches`
        The underlying C++ factory function.
    """

    def __init__(
        self, event_type: EventType, batch_size: int | Param[int]
    ) -> None:
        self._event_type = event_type
        self._batch_size = batch_size

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._batch_size, Param):
            return ((self._batch_size, _size_type),)
        return ()

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (self._event_type,), self.__class__.__name__
        )
        return (self._event_type,)

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        batch_size = gencontext.size_t_expression(self._batch_size)
        return _CppExpression(
            f"""\
            tcspc::process_in_batches<{self._event_type._cpp_type_name()}>(
                tcspc::arg::batch_size<std::size_t>{{{batch_size}}},
                {downstream}
            )"""
        )
