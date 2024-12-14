# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "CheckMonotonic",
    "Count",
    "DecodeBHSPC",
    "NullSink",
    "ReadBinaryStream",
    "SinkEvents",
    "Stop",
    "StopWithError",
    "Unbatch",
    "read_events_from_binary_file",
]

from collections.abc import Collection, Iterable, Sequence
from typing import final

from typing_extensions import override

from . import _access, _cpp_utils, _events, _streams
from ._access import Access, AccessTag
from ._acquisition_readers import AcquisitionReader, PyAcquisitionReader
from ._bucket_sources import BucketSource, RecyclingBucketSource
from ._codegen import CodeGenerationContext
from ._cpp_utils import (
    CppExpression,
    CppTypeName,
    size_type,
    string_type,
    uint64_type,
)
from ._data_types import DataTypes
from ._events import BucketEvent, EventType, WarningEvent
from ._graph import Graph, Subgraph
from ._node import Node, RelayNode, TypePreservingRelayNode
from ._param import Param


def _check_events_subset_of(
    input_events: Iterable[EventType],
    allowed_events: Iterable[EventType],
    processor: str,
) -> None:
    for t in input_events:
        if not _cpp_utils.contains_type(
            (u.cpp_type_name() for u in allowed_events), t.cpp_type_name()
        ):
            raise ValueError(f"input type {t} not accepted by {processor}")


def _remove_events_from_set(
    input_events: Iterable[EventType], events_to_remove: Iterable[EventType]
) -> tuple[EventType, ...]:
    return tuple(
        t
        for t in input_events
        if not _cpp_utils.contains_type(
            (u.cpp_type_name() for u in events_to_remove), t.cpp_type_name()
        )
    )


def _make_type_list(event_types: Iterable[EventType]) -> CppTypeName:
    return CppTypeName(
        "tcspc::type_list<{}>".format(
            ", ".join(t.cpp_type_name() for t in event_types)
        )
    )


def read_events_from_binary_file(
    event_type: EventType,
    filename: str | Param[str],
    *,
    start_offset: int | Param[int] = 0,
    max_length: int | Param[int] | None = None,
    read_granularity_bytes: int | Param[int] = 65536,
    stop_normally_on_error: bool = False,
) -> Node:
    g = Graph()
    g.add_sequence(
        [
            (
                "reader",
                ReadBinaryStream(
                    event_type,
                    _streams.BinaryFileInputStream(
                        filename, start_offset=start_offset
                    ),
                    max_length,
                    RecyclingBucketSource(event_type),
                    read_granularity_bytes,
                ),
            ),
            (
                Stop((WarningEvent,), "error reading input")
                if stop_normally_on_error
                else StopWithError(
                    (WarningEvent,),
                    CppTypeName("std::runtime_error"),
                    "error reading input",
                )
            ),
            (
                "unbatcher",
                Unbatch(BucketEvent(event_type)),
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
class Acquire(RelayNode):
    def __init__(
        self,
        event_type: EventType,
        reader: AcquisitionReader | Param[PyAcquisitionReader],
        buffer_provider: BucketSource | None,
        batch_size: int | Param[int] | None,
        access_tag: _access.AccessTag,
    ) -> None:
        self._event_type = event_type
        self._reader = reader
        self._bucket_source = (
            buffer_provider
            if buffer_provider is not None
            else RecyclingBucketSource(event_type)
        )
        self._batch_size = batch_size if batch_size is not None else 65536
        self._access_tag = access_tag

    @override
    def accesses(self) -> Sequence[tuple[AccessTag, type[Access]]]:
        return ((self._access_tag, _access.AcquireAccess),)

    @override
    def relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(input_event_set, (), self.__class__.__name__)
        return (_events.BucketEvent(self._event_type),)

    def _buffer_array_type(self) -> CppTypeName:
        return CppTypeName(f"""\
            nanobind::ndarray<
                {self._event_type.cpp_type_name()},
                nanobind::numpy, nanobind::device::cpu, nanobind::c_contig>""")

    def _buffer_array_param_type(self) -> CppTypeName:
        return CppTypeName(f"""\
            decltype(std::declval<{self._buffer_array_type()}>()
                .cast(nanobind::rv_policy::reference))""")

    @override
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        params: list[tuple[Param, CppTypeName]] = []
        if isinstance(self._reader, Param):
            params.append(
                (
                    self._reader,
                    CppTypeName(
                        f"""\
                        std::function<
                            auto({self._buffer_array_param_type()})
                            -> std::optional<std::size_t>>"""
                    ),
                )
            )
        if isinstance(self._batch_size, Param):
            params.append((self._batch_size, size_type))
        return params

    @override
    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        reader = (
            CppExpression(
                f"""\
                [reader={gencontext.params_varname}.{self._reader.cpp_identifier()}](
                    tcspc::span<{self._event_type.cpp_type_name()}> spn) {{
                    nanobind::gil_scoped_acquire held;
                    {self._buffer_array_type()} arr(spn.data(), {{spn.size()}});
                    return reader(arr.cast(nanobind::rv_policy::reference));
                }}
                """
            )
            if isinstance(self._reader, Param)
            else self._reader.cpp_expression()
        )
        batch_size = gencontext.size_t_expression(self._batch_size)
        return CppExpression(
            f"""\
            tcspc::acquire<{self._event_type.cpp_type_name()}>(
                {reader},
                {self._bucket_source.cpp_expression(gencontext)},
                tcspc::arg::batch_size{{{batch_size}}},
                {gencontext.tracker_expression(CppTypeName("tcspc::acquire_access"), self._access_tag)},
                {downstream}
            )"""
        )


@final
class CheckMonotonic(TypePreservingRelayNode):
    def __init__(self, data_types: DataTypes | None = None) -> None:
        self._data_types = (
            data_types if data_types is not None else DataTypes()
        )

    @override
    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        return CppExpression(
            f"""\
            tcspc::check_monotonic<{self._data_types.cpp_type_name()}>(
                {downstream}
            )"""
        )


@final
class Count(TypePreservingRelayNode):
    def __init__(self, event_type: EventType, access_tag: AccessTag) -> None:
        self._event_type = event_type
        self._access_tag = access_tag

    @override
    def accesses(
        self,
    ) -> Sequence[tuple[AccessTag, type[Access]]]:
        return ((self._access_tag, _access.CountAccess),)

    @override
    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        return CppExpression(
            f"""\
            tcspc::count<{self._event_type.cpp_type_name()}>(
                {gencontext.tracker_expression(CppTypeName("tcspc::count_access"), self._access_tag)},
                {downstream}
            )"""
        )


@final
class DecodeBHSPC(RelayNode):
    def __init__(self, data_types: DataTypes | None = None) -> None:
        self._data_types = (
            data_types if data_types is not None else DataTypes()
        )

    @override
    def relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(
            input_event_set, (_events.BHSPCEvent,), self.__class__.__name__
        )
        return (
            _events.DataLostEvent(self._data_types),
            _events.MarkerEvent(self._data_types),
            _events.TimeCorrelatedDetectionEvent(self._data_types),
            _events.TimeReachedEvent(self._data_types),
            WarningEvent,
        )

    @override
    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        return CppExpression(
            f"""\
            tcspc::decode_bh_spc<{self._data_types.cpp_type_name()}>(
                {downstream}
            )"""
        )


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
    def cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstreams: Sequence[CppExpression],
    ) -> CppExpression:
        return CppExpression("tcspc::null_sink()")


@final
class ReadBinaryStream(RelayNode):
    def __init__(
        self,
        event_type: EventType,
        stream: _streams.InputStream,
        max_length: int | Param[int] | None = None,
        buffer_provider: BucketSource | None = None,
        read_granularity_bytes: int | Param[int] = 65536,
    ):
        self._event_type = event_type
        self._stream = stream
        self._maxlen = max_length
        self._bucket_source = (
            buffer_provider
            if buffer_provider is not None
            else RecyclingBucketSource(event_type)
        )
        self._granularity = read_granularity_bytes

    @override
    def relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        _check_events_subset_of(input_event_set, (), self.__class__.__name__)
        return (BucketEvent(self._event_type),)

    @override
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        params: list[tuple[Param, CppTypeName]] = []
        if isinstance(self._maxlen, Param):
            params.append((self._maxlen, uint64_type))
        if isinstance(self._granularity, Param):
            params.append((self._granularity, size_type))
        params.extend(self._stream.parameters())
        params.extend(self._bucket_source.parameters())
        return params

    @override
    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        maxlen = (
            "std::numeric_limits<tcspc::u64>::max()"
            if self._maxlen is None
            else gencontext.u64_expression(self._maxlen)
        )
        granularity = gencontext.size_t_expression(self._granularity)
        return CppExpression(
            f"""\
            tcspc::read_binary_stream<{self._event_type.cpp_type_name()}>(
                {self._stream.cpp_expression(gencontext)},
                tcspc::arg::max_length<tcspc::u64>{{{maxlen}}},
                {self._bucket_source.cpp_expression(gencontext)},
                tcspc::arg::granularity<std::size_t>{{{granularity}}},
                {downstream}
            )"""
        )


@final
class SinkEvents(Node):
    def __init__(self, *event_types: EventType) -> None:
        super().__init__(output=())
        self._event_types = tuple(event_types)

    @override
    def map_event_sets(
        self, input_event_sets: Sequence[Collection[EventType]]
    ) -> tuple[tuple[EventType, ...], ...]:
        if len(input_event_sets) != 1:
            raise ValueError(
                f"wrong number of inputs (1 expected, {len(input_event_sets)} found)"
            )
        _check_events_subset_of(
            input_event_sets[0], self._event_types, self.__class__.__name__
        )
        return ()

    @override
    def cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstreams: Sequence[CppExpression],
    ) -> CppExpression:
        evts = ", ".join(t.cpp_type_name() for t in self._event_types)
        return CppExpression(f"tcspc::sink_events<{evts}>()")


@final
class Stop(RelayNode):
    def __init__(
        self,
        event_types: Iterable[EventType],
        message_prefix: str | Param[str],
    ) -> None:
        self._event_types = list(event_types)
        self._msg_prefix = message_prefix

    @override
    def relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        if isinstance(self._msg_prefix, Param):
            return ((self._msg_prefix, string_type),)
        return ()

    @override
    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        return CppExpression(
            f"""\
            tcspc::stop<
                {_make_type_list(self._event_types)}
            >(
                {gencontext.string_expression(self._msg_prefix)},
                {downstream}
            )"""
        )


@final
class StopWithError(RelayNode):
    def __init__(
        self,
        event_types: Iterable[EventType],
        exception_type: CppTypeName,
        message_prefix: str | Param[str],
    ) -> None:
        self._event_types = list(event_types)
        self._exception_type = exception_type
        self._msg_prefix = message_prefix

    @override
    def relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        return _remove_events_from_set(input_event_set, self._event_types)

    @override
    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        if isinstance(self._msg_prefix, Param):
            return ((self._msg_prefix, string_type),)
        return ()

    @override
    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        return CppExpression(
            f"""\
            tcspc::stop_with_error<
                {_make_type_list(self._event_types)}
                {self._exception_type}
            >(
                {gencontext.string_expression(self._msg_prefix)},
                {downstream}
            )"""
        )


@final
class Unbatch(RelayNode):
    def __init__(self, event_type: BucketEvent) -> None:
        self._event_type = event_type

    @override
    def relay_map_event_set(
        self, input_event_set: Collection[EventType]
    ) -> tuple[EventType, ...]:
        for ie in input_event_set:
            if ie != self._event_type:
                raise ValueError("incorrect input event type")
        return (self._event_type.element_event_type(),)

    @override
    def relay_cpp_expression(
        self,
        gencontext: CodeGenerationContext,
        downstream: CppExpression,
    ) -> CppExpression:
        return CppExpression(
            f"""\
            tcspc::unbatch<{self._event_type.cpp_type_name()}>(
                {downstream}
            )"""
        )
