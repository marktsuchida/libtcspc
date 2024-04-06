# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Collection, Iterable, Sequence
from textwrap import dedent
from typing import final

import cppyy
from typing_extensions import override

from . import _access, _cpp_utils, _events, _misc, _streams
from ._data_traits import DataTraits
from ._events import EventType
from ._graph import (
    Graph,
    Node,
    OneToOneNode,
    OneToOnePassThroughNode,
    Subgraph,
)

cppyy.include("libtcspc/tcspc.hpp")

cppyy.include("cstdint")
cppyy.include("limits")
cppyy.include("type_traits")


def _check_events_subset_of(
    input_events: Iterable[EventType],
    allowed_events: Iterable[EventType],
    processor: str,
) -> None:
    for t in input_events:
        if not _cpp_utils.contains_type(
            (u.cpp_type for u in allowed_events), t.cpp_type
        ):
            raise ValueError(f"input type {t} not accepted by {processor}")


def _remove_events_from_set(
    input_events: Iterable[EventType], events_to_remove: Iterable[EventType]
) -> tuple[EventType, ...]:
    return tuple(
        t
        for t in input_events
        if not _cpp_utils.contains_type(
            (u.cpp_type for u in events_to_remove), t.cpp_type
        )
    )


def _make_type_list(event_types: Iterable[EventType]) -> str:
    return "tcspc::type_list<{}>".format(
        ", ".join(t.cpp_type for t in event_types)
    )


def read_events_from_binary_file(
    event_type: EventType,
    filename: str,
    *,
    start_offset: int = 0,
    max_length: int = -1,
    read_granularity_bytes: int = 65536,
    stop_normally_on_error: bool = False,
) -> Node:
    g = Graph()
    stop_proc = Stop if stop_normally_on_error else StopWithError
    g.add_sequence(
        [
            (
                "reader",
                ReadBinaryStream(
                    event_type,
                    _events.VectorEvent(event_type),
                    _streams.BinaryFileInputStream(
                        filename, start=start_offset
                    ),
                    max_length,
                    _misc.ObjectPool(
                        _events.VectorEvent(event_type),
                        initial_count=0,
                        max_count=2,
                    ),
                    read_granularity_bytes,
                ),
            ),
            stop_proc((_events.WarningEvent,), "error reading input"),
            DereferencePointer(
                _events.SharedPtrEvent(_events.VectorEvent(event_type))
            ),
            (
                "unbatcher",
                Unbatch(event_type),
            ),
        ]
    )
    return Subgraph(
        g,
        input_map={"input": ("reader", "input")},
        output_map={"output": ("unbatcher", "output")},
    )


# Note: Wrappers of C++ processors are ordered alphabetically without regard to
# the C++ header in which they are defined.


@final
class CheckMonotonic(OneToOnePassThroughNode):
    def __init__(self, data_traits: DataTraits | None = None) -> None:
        self._data_traits = (
            data_traits if data_traits is not None else DataTraits()
        )

    @override
    def generate_cpp_one_to_one(
        self, node_name: str, context: str, downstream: str
    ) -> str:
        return dedent(f"""\
            tcspc::check_monotonic<{self._data_traits.cpp()}>(
                {downstream}
            )""")


@final
class Count(OneToOnePassThroughNode):
    def __init__(self, event_type: EventType) -> None:
        self._event_type = event_type

    @override
    def access_type(self) -> type[_access.Access] | None:
        return _access.CountAccess

    @override
    def generate_cpp_one_to_one(
        self, node_name: str, context: str, downstream: str
    ) -> str:
        return dedent(f"""\
            tcspc::count<{self._event_type.cpp_type}>(
                {context}->tracker<tcspc::count_access>("{node_name}"),
                {downstream}
            )""")


@final
class DecodeBHSPC(OneToOneNode):
    def __init__(self, data_traits: DataTraits | None = None) -> None:
        self._data_traits = (
            data_traits if data_traits is not None else DataTraits()
        )

    @override
    def map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (_events.BHSPCEvent,), self.__class__.__name__
        )
        return (
            _events.DataLostEvent(self._data_traits),
            _events.MarkerEvent(self._data_traits),
            _events.TimeCorrelatedDetectionEvent(self._data_traits),
            _events.TimeReachedEvent(self._data_traits),
            _events.WarningEvent,
        )

    @override
    def generate_cpp_one_to_one(
        self, node_name: str, context: str, downstream: str
    ) -> str:
        return dedent(f"""\
            tcspc::decode_bh_spc<{self._data_traits.cpp()}>(
                {downstream}
            )""")


@final
class DereferencePointer(OneToOneNode):
    def __init__(self, ptr_type: EventType) -> None:
        self._ptr_type = ptr_type

    @override
    def map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (self._ptr_type,), self.__class__.__name__
        )
        ptr = self._ptr_type.cpp_type
        return (
            EventType(
                f"std::remove_reference_t<decltype(*std::declval<{ptr}>())>"
            ),
        )

    @override
    def generate_cpp_one_to_one(
        self, node_name: str, context: str, downstream: str
    ) -> str:
        return dedent(f"""\
            tcspc::dereference_pointer<{self._ptr_type.cpp_type}>(
                {downstream}
            )""")


@final
class NullSink(Node):
    def __init__(self) -> None:
        super().__init__(output=())

    @override
    def map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        return ()

    @override
    def generate_cpp(
        self, node_name: str, context: str, downstreams: Sequence[str]
    ) -> str:
        return "tcspc::null_sink()"


@final
class ReadBinaryStream(OneToOneNode):
    def __init__(
        self,
        event_type: EventType,
        event_vector_type: EventType,
        stream: _streams.InputStream,
        max_length: int,
        buffer_pool: _misc.ObjectPool,
        read_granularity_bytes: int,
    ):
        self._event_type = event_type
        self._event_vector_type = event_vector_type
        self._stream = stream
        self._maxlen = max_length
        self._pool = buffer_pool
        self._granularity = read_granularity_bytes

    @override
    def map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(input_event_set, (), self.__class__.__name__)
        return (_events.SharedPtrEvent(_events.VectorEvent(self._event_type)),)

    @override
    def generate_cpp_one_to_one(
        self, node_name: str, context: str, downstream: str
    ) -> str:
        event = self._event_type.cpp_type
        vector = self._event_vector_type.cpp_type
        maxlen = (
            self._maxlen
            if self._maxlen >= 0
            else "std::numeric_limits<std::uint64_t>::max()"
        )
        return dedent(f"""\
            tcspc::read_binary_stream<{event}, {vector}>(
                {self._stream.cpp},
                {maxlen},
                {self._pool.cpp},
                {self._granularity},
                {downstream}
            )""")


@final
class Stop(OneToOneNode):
    def __init__(
        self, event_types: Iterable[EventType], message_prefix: str
    ) -> None:
        self._event_types = list(event_types)
        self._msg_prefix = message_prefix

    @override
    def map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def generate_cpp_one_to_one(
        self, node_name: str, context: str, downstream: str
    ) -> str:
        prefix = _cpp_utils.quote_string(self._msg_prefix)
        return dedent(f"""\
            tcspc::stop<
                {_make_type_list(self._event_types)}
            >(
                {prefix},
                {downstream}
            )""")


@final
class StopWithError(OneToOneNode):
    def __init__(
        self,
        event_types: Iterable[EventType],
        exception_type: str,
        message_prefix: str,
    ) -> None:
        self._event_types = list(event_types)
        self._exception_type = exception_type
        self._msg_prefix = message_prefix

    @override
    def map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def generate_cpp_one_to_one(
        self, node_name: str, context: str, downstream: str
    ) -> str:
        prefix = _cpp_utils.quote_string(self._msg_prefix)
        return dedent(f"""\
            tcspc::stop_with_error<
                {_make_type_list(self._event_types)}
                {self._exception_type}
            >(
                {prefix},
                {downstream}
            )""")


@final
class Unbatch(OneToOneNode):
    def __init__(self, event_type: EventType) -> None:
        self._event_type = event_type

    @override
    def map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        # TODO Check if input event set contains only iterables of event_type.
        return (self._event_type,)

    @override
    def generate_cpp_one_to_one(
        self, node_name: str, context: str, downstream: str
    ) -> str:
        return dedent(f"""\
            tcspc::unbatch<{self._event_type.cpp_type}>(
                {downstream}
            )""")
