# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import ABC, abstractmethod

from typing_extensions import override

from . import _cpp_utils
from ._cpp_utils import (
    _CppClassScopeDefs,
    _CppIdentifier,
    _CppTypeName,
)
from ._data_types import DataTypes


class EventType(ABC):
    """Opaque marker for the type of events carried on a graph edge.

    ``EventType`` itself is abstract; instantiate one of its concrete
    subclasses.

    User-defined event types are not supported.
    """

    @abstractmethod
    def _cpp_type_name(self) -> _CppTypeName: ...

    def __repr__(self) -> str:
        return f"<{type(self).__name__}({self._cpp_type_name()})>"

    def __eq__(self, other: object) -> bool:
        return isinstance(other, EventType) and _cpp_utils._is_same_type(
            self._cpp_type_name(), other._cpp_type_name()
        )

    def _cpp_input_handler(
        self, downstream: _CppIdentifier
    ) -> _CppClassScopeDefs:
        return _CppClassScopeDefs(f"""\
        void handle({self._cpp_type_name()} const &event) {{
            {downstream}.handle(event);
        }}
        """)

    def _cpp_output_handlers(
        self, pysink: _CppIdentifier
    ) -> _CppClassScopeDefs:
        return _CppClassScopeDefs(f"""\
        void handle({self._cpp_type_name()} const &event) {{
            nanobind::gil_scoped_acquire held;
            {pysink}->handle(
                nanobind::cast(event, nanobind::rv_policy::copy));
        }}

        void handle({self._cpp_type_name()} &&event) {{
            nanobind::gil_scoped_acquire held;
            {pysink}->handle(
                nanobind::cast(std::move(event), nanobind::rv_policy::move));
        }}
        """)


class BucketEvent(EventType):
    def __init__(self, element_type: EventType) -> None:
        self._element_type = element_type

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::bucket<{self._element_type._cpp_type_name()}>"
        )

    @override
    def _cpp_input_handler(
        self, downstream: _CppIdentifier
    ) -> _CppClassScopeDefs:
        elem_cpp_type = self._element_type._cpp_type_name()
        ndarray_type = _CppTypeName(
            f"nanobind::ndarray<{elem_cpp_type} const, nanobind::device::cpu, nanobind::c_contig>"
        )
        return _CppClassScopeDefs(f"""\
        void handle({ndarray_type} const &event) {{
            // Emit bucket<T>, not bucket<T const>, but by const ref.
            auto *const ptr = const_cast<{elem_cpp_type} *>(event.data());
            auto const spn = std::span(ptr, event.size());
            auto const bkt = tcspc::ad_hoc_bucket(spn);
            {downstream}.handle(bkt);
        }}
        """)

    @override
    def _cpp_output_handlers(
        self, pysink: _CppIdentifier
    ) -> _CppClassScopeDefs:
        elem_cpp_type = self._element_type._cpp_type_name()
        # We take ownership of the bucket if we can; otherwise we make a copy;
        # in all cases we emit a writable ndarray that owns the memory.
        # TODO Once we support Python bucket sources, buckets from them should
        # be handled specially to eliminate copying (and set read-only as
        # appropriate).
        return _CppClassScopeDefs(f"""\
        void emit_span_copy(std::span<{elem_cpp_type} const> spn) {{
            using elem_type = {elem_cpp_type};
            auto *buf = new elem_type[spn.size()];
            std::copy(spn.begin(), spn.end(), buf);
            nanobind::gil_scoped_acquire held;
            auto deleter = nanobind::capsule(buf,
                [](void *p) noexcept {{ delete[] static_cast<elem_type *>(p); }});
            {pysink}->handle(nanobind::ndarray<elem_type, nanobind::numpy>(
                buf, {{spn.size()}}, deleter).cast());

        }}

        void handle(tcspc::bucket<{elem_cpp_type} const> const &event) {{
            emit_span_copy(
                std::span<{elem_cpp_type} const>(event.begin(), event.end()));
        }}

        void handle(tcspc::bucket<{elem_cpp_type}> const &event) {{
            emit_span_copy(
                std::span<{elem_cpp_type} const>(event.begin(), event.end()));
        }}

        void handle(tcspc::bucket<{elem_cpp_type}> &&event) {{
            using elem_type = {elem_cpp_type};
            using bkt_type = tcspc::bucket<elem_type>;
            auto *bkt = new bkt_type(std::move(event));
            nanobind::gil_scoped_acquire held;
            auto deleter = nanobind::capsule(bkt,
                [](void *p) noexcept {{ delete static_cast<bkt_type *>(p); }});
            {pysink}->handle(nanobind::ndarray<elem_type, nanobind::numpy>(
                bkt->data(), {{bkt->size()}}, deleter).cast());
        }}
        """)

    def element_event_type(self) -> EventType:
        return self._element_type


# Note: C++ event wrappers are ordered alphabetically without regard to the C++
# header in which they are defined.


class BHSPCEvent(EventType):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::bh_spc_event")


class DataLostEvent(EventType):
    def __init__(self, data_types: DataTypes | None = None) -> None:
        self._data_types = (
            data_types if data_types is not None else DataTypes()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::data_lost_event<{self._data_types._cpp_type_name()}>"
        )


class MarkerEvent(EventType):
    def __init__(self, data_types: DataTypes | None = None) -> None:
        self._data_types = (
            data_types if data_types is not None else DataTypes()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::marker_event<{self._data_types._cpp_type_name()}>"
        )


class TimeCorrelatedDetectionEvent(EventType):
    def __init__(self, data_types: DataTypes | None = None) -> None:
        self._data_types = (
            data_types if data_types is not None else DataTypes()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::time_correlated_detection_event<{self._data_types._cpp_type_name()}>"
        )


class TimeReachedEvent(EventType):
    def __init__(self, data_types: DataTypes | None = None) -> None:
        self._data_types = (
            data_types if data_types is not None else DataTypes()
        )

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::time_reached_event<{self._data_types._cpp_type_name()}>"
        )


class WarningEvent(EventType):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::warning_event")
