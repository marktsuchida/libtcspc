# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Iterable, Sequence
from typing import final

from typing_extensions import override

from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppExpression,
    _CppTypeName,
    _string_type,
)
from .._events import EventType
from .._node import _RelayNode
from .._param import Param
from ._common import (
    _make_type_list,
    _remove_events_from_set,
)


@final
class Stop(_RelayNode):
    """Pass-through processor that ends the stream normally on a configured event.

    When an event matching one of ``event_types`` is received, the
    downstream is flushed and processing ends normally (the Python
    binding surfaces this as `EndOfProcessing`, with the triggering
    event included in the message). All other events pass through
    unchanged.

    Parameters
    ----------
    event_types : Iterable[EventType]
        Event types that trigger normal termination.
    message_prefix : str or Param[str]
        String prepended to the `EndOfProcessing` exception's message,
        typically describing the source of the termination.

    Notes
    -----
    Events handled:

    - Events matching one of ``event_types``: flush the downstream, then
      end processing normally.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::stop`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_types: Iterable[EventType],
        message_prefix: str | Param[str],
    ) -> None:
        self._event_types = list(event_types)
        self._msg_prefix = message_prefix

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._msg_prefix, Param):
            return ((self._msg_prefix, _string_type),)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::stop<
                {_make_type_list(self._event_types)}
            >(
                {gencontext.string_expression(self._msg_prefix)},
                {downstream}
            )"""
        )


@final
class StopWithError(_RelayNode):
    """Pass-through processor that ends the stream with an error on a configured event.

    When an event matching one of ``event_types`` is received, processing
    ends with an error: a `RuntimeError`-equivalent is raised on the
    Python side, with ``message_prefix`` included in the message. Unlike
    `Stop`, the downstream is **not** flushed. All other events pass
    through unchanged.

    Parameters
    ----------
    event_types : Iterable[EventType]
        Event types that trigger error termination.
    message_prefix : str or Param[str]
        String prepended to the raised exception's message, typically
        describing the source of the error condition.

    Notes
    -----
    Events handled:

    - Events matching one of ``event_types``: end processing with an
      error. The downstream is **not** flushed.
    - All other event types: pass through unchanged.
    - End of input: pass through.

    See Also
    --------
    :cpp:`tcspc::stop_with_error`
        The underlying C++ factory function.
    """

    def __init__(
        self,
        event_types: Iterable[EventType],
        message_prefix: str | Param[str],
    ) -> None:
        self._event_types = list(event_types)
        self._msg_prefix = message_prefix

    @override
    def _relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        if isinstance(self._msg_prefix, Param):
            return ((self._msg_prefix, _string_type),)
        return ()

    @override
    def _relay_cpp_expression(
        self,
        gencontext: _CodeGenerationContext,
        downstream: _CppExpression,
    ) -> _CppExpression:
        return _CppExpression(
            f"""\
            tcspc::stop_with_error<
                {_make_type_list(self._event_types)}
            >(
                {gencontext.string_expression(self._msg_prefix)},
                {downstream}
            )"""
        )
