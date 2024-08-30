# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import cppyy

from . import _cpp_utils
from ._cpp_utils import CppTypeName
from ._data_types import DataTypes

cppyy.include("memory")
cppyy.include("vector")


class EventType:
    def __init__(self, cpp_type: CppTypeName) -> None:
        self._cpp_type = cpp_type

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}({self.cpp_type})>"

    def __eq__(self, other) -> bool:
        return isinstance(other, EventType) and _cpp_utils.is_same_type(
            self.cpp_type, other.cpp_type
        )

    @property
    def cpp_type(self) -> CppTypeName:
        return self._cpp_type


class BucketEvent(EventType):
    def __init__(self, element_type: EventType) -> None:
        super().__init__(
            CppTypeName(f"tcspc::bucket<{element_type.cpp_type}>")
        )


# Note: C++ event wrappers are ordered alphabetically without regard to the C++
# header in which they are defined.


BHSPCEvent = EventType(CppTypeName("tcspc::bh_spc_event"))


def DataLostEvent(data_types: DataTypes | None = None) -> EventType:
    if data_types is None:
        data_types = DataTypes()
    return EventType(
        CppTypeName(f"tcspc::data_lost_event<{data_types.cpp()}>")
    )


def MarkerEvent(data_types: DataTypes | None = None) -> EventType:
    if data_types is None:
        data_types = DataTypes()
    return EventType(CppTypeName(f"tcspc::marker_event<{data_types.cpp()}>"))


def TimeCorrelatedDetectionEvent(
    data_types: DataTypes | None = None,
) -> EventType:
    if data_types is None:
        data_types = DataTypes()
    return EventType(
        CppTypeName(
            f"tcspc::time_correlated_detection_event<{data_types.cpp()}>"
        )
    )


def TimeReachedEvent(data_types: DataTypes | None = None) -> EventType:
    if data_types is None:
        data_types = DataTypes()
    return EventType(
        CppTypeName(f"tcspc::time_reached_event<{data_types.cpp()}>")
    )


WarningEvent = EventType(CppTypeName("tcspc::warning_event"))
