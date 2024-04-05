# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import cppyy

from . import _cpp_utils
from ._data_traits import DataTraits

cppyy.include("memory")
cppyy.include("vector")


class EventType:
    def __init__(self, cpp_type: str) -> None:
        self._cpp_type = cpp_type

    def __repr__(self) -> str:
        return f"<{self.__class__.__name__}({self.cpp_type})>"

    def __eq__(self, other) -> bool:
        return isinstance(other, EventType) and _cpp_utils.is_same_type(
            self.cpp_type, other.cpp_type
        )

    @property
    def cpp_type(self) -> str:
        return self._cpp_type


class SharedPtrEvent(EventType):
    def __init__(self, element_type: EventType) -> None:
        super().__init__(f"std::shared_ptr<{element_type.cpp_type}>")


class VectorEvent(EventType):
    def __init__(self, element_type: EventType) -> None:
        super().__init__(f"std::vector<{element_type.cpp_type}>")


# Note: C++ event wrappers are ordered alphabetically without regard to the C++
# header in which they are defined.


BHSPCEvent = EventType("tcspc::bh_spc_event")


def DataLostEvent(data_traits: DataTraits | None = None) -> EventType:
    if data_traits is None:
        data_traits = DataTraits()
    return EventType(f"tcspc::data_lost_event<{data_traits.cpp()}>")


def MarkerEvent(data_traits: DataTraits | None = None) -> EventType:
    if data_traits is None:
        data_traits = DataTraits()
    return EventType(f"tcspc::marker_event<{data_traits.cpp()}>")


def TimeCorrelatedDetectionEvent(
    data_traits: DataTraits | None = None,
) -> EventType:
    if data_traits is None:
        data_traits = DataTraits()
    return EventType(
        f"tcspc::time_correlated_detection_event<{data_traits.cpp()}>"
    )


def TimeReachedEvent(data_traits: DataTraits | None = None) -> EventType:
    if data_traits is None:
        data_traits = DataTraits()
    return EventType(f"tcspc::time_reached_event<{data_traits.cpp()}>")


WarningEvent = EventType("tcspc::warning_event")
