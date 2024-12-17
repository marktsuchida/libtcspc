# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "EventType",
    "BucketEvent",
    "BHSPCEvent",
    "DataLostEvent",
    "MarkerEvent",
    "TimeCorrelatedDetectionEvent",
    "TimeReachedEvent",
    "WarningEvent",
]

from typing_extensions import override

from . import _cpp_utils
from ._cpp_utils import CppClassScopeDefs, CppIdentifier, CppTypeName
from ._data_types import DataTypes


class EventType:
    def __init__(self, cpp_type: CppTypeName) -> None:
        self._cpp_type = cpp_type

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}({self.cpp_type_name()})>"

    def __eq__(self, other) -> bool:
        return isinstance(other, EventType) and _cpp_utils.is_same_type(
            self.cpp_type_name(), other.cpp_type_name()
        )

    def cpp_type_name(self) -> CppTypeName:
        return self._cpp_type

    def cpp_input_handler(
        self, downstream: CppIdentifier
    ) -> CppClassScopeDefs:
        return CppClassScopeDefs(f"""\
        void handle({self.cpp_type_name()} const &event) {{
            {downstream}.handle(event);
        }}
        """)


class BucketEvent(EventType):
    def __init__(self, element_type: CppTypeName | EventType) -> None:
        if not isinstance(element_type, EventType):
            element_type = EventType(element_type)
        self._element_type = element_type
        super().__init__(
            CppTypeName(f"tcspc::bucket<{element_type.cpp_type_name()}>")
        )

    @override
    def cpp_input_handler(
        self, downstream: CppIdentifier
    ) -> CppClassScopeDefs:
        elem_cpp_type = self._element_type.cpp_type_name()
        ndarray_type = CppTypeName(
            f"nanobind::ndarray<{elem_cpp_type} const, nanobind::device::cpu, nanobind::c_contig>"
        )
        return CppClassScopeDefs(f"""\
        void handle({ndarray_type} const &event) {{
            // Emit bucket<T>, not bucket<T const>, but by const ref.
            auto *const ptr = const_cast<{elem_cpp_type} *>(event.data());
            auto const spn = tcspc::span(ptr, event.size());
            auto const bkt = tcspc::ad_hoc_bucket(spn);
            {downstream}.handle(bkt);
        }}
        """)

    def element_event_type(self) -> EventType:
        return self._element_type


# Note: C++ event wrappers are ordered alphabetically without regard to the C++
# header in which they are defined.


BHSPCEvent = EventType(CppTypeName("tcspc::bh_spc_event"))


def DataLostEvent(data_types: DataTypes | None = None) -> EventType:
    if data_types is None:
        data_types = DataTypes()
    return EventType(
        CppTypeName(f"tcspc::data_lost_event<{data_types.cpp_type_name()}>")
    )


def MarkerEvent(data_types: DataTypes | None = None) -> EventType:
    if data_types is None:
        data_types = DataTypes()
    return EventType(
        CppTypeName(f"tcspc::marker_event<{data_types.cpp_type_name()}>")
    )


def TimeCorrelatedDetectionEvent(
    data_types: DataTypes | None = None,
) -> EventType:
    if data_types is None:
        data_types = DataTypes()
    return EventType(
        CppTypeName(
            f"tcspc::time_correlated_detection_event<{data_types.cpp_type_name()}>"
        )
    )


def TimeReachedEvent(data_types: DataTypes | None = None) -> EventType:
    if data_types is None:
        data_types = DataTypes()
    return EventType(
        CppTypeName(f"tcspc::time_reached_event<{data_types.cpp_type_name()}>")
    )


WarningEvent = EventType(CppTypeName("tcspc::warning_event"))
