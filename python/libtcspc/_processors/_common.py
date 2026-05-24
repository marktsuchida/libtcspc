# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Iterable

from .. import _cpp_utils
from .._bucket_sources import (
    BucketSource,
    PyBucketSource,
    RecyclingBucketSource,
    _PyBucketSource,
)
from .._codegen import _CodeGenerationContext
from .._cpp_utils import (
    _CppTypeName,
)
from .._events import EventType
from .._param import Param


def _check_events_subset_of(
    input_events: Iterable[EventType],
    allowed_events: Iterable[EventType],
    processor: str,
) -> None:
    for t in input_events:
        if not _cpp_utils._contains_type(
            (u._cpp_type_name() for u in allowed_events), t._cpp_type_name()
        ):
            raise ValueError(f"input type {t} not accepted by {processor}")


def _remove_events_from_set(
    input_events: Iterable[EventType], events_to_remove: Iterable[EventType]
) -> tuple[EventType, ...]:
    return tuple(
        t
        for t in input_events
        if not _cpp_utils._contains_type(
            (u._cpp_type_name() for u in events_to_remove), t._cpp_type_name()
        )
    )


def _make_type_list(event_types: Iterable[EventType]) -> _CppTypeName:
    return _CppTypeName(
        "tcspc::type_list<{}>".format(
            ", ".join(t._cpp_type_name() for t in event_types)
        )
    )


def _bucket_source_or_default(
    event_type: EventType,
    arg: BucketSource | Param[PyBucketSource] | None,
) -> BucketSource:
    if isinstance(arg, Param):
        return _PyBucketSource(event_type, arg)
    return arg if arg is not None else RecyclingBucketSource(event_type)


def _with_event_added(
    input_events: Iterable[EventType], event: EventType
) -> tuple[EventType, ...]:
    events = tuple(input_events)
    if _cpp_utils._contains_type(
        (t._cpp_type_name() for t in events), event._cpp_type_name()
    ):
        return events
    return (*events, event)


def _double_expr(
    gencontext: _CodeGenerationContext, v: float | Param[float]
) -> str:
    if isinstance(v, Param):
        return f"{gencontext.params_varname}.{v._cpp_identifier()}"
    return repr(float(v))


def _cast_int_expr(
    gencontext: _CodeGenerationContext,
    v: int | Param[int],
    cpp_type: str,
) -> str:
    if isinstance(v, Param):
        inner = f"{gencontext.params_varname}.{v._cpp_identifier()}"
    else:
        inner = str(v)
    return f"static_cast<{cpp_type}>({inner})"
