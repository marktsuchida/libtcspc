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

    An `EventType` instance is a type tag, not a data carrier. Each concrete
    subclass corresponds to a C++ event type in ``namespace tcspc``; an
    instance describes a particular instantiation of that C++ type (for
    example, parameterised by `DataTypes`) and is used by the codegen layer
    to determine the C++ types flowing on each graph edge.

    ``EventType`` itself is abstract; instantiate one of its concrete
    subclasses. User-defined event types are not supported.
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
    """Event carrying a contiguous array of elements of another event type.

    Emitted by sources and batching processors (for example,
    `ReadBinaryStream`, `Batch`, and `Acquire`) to transport bulk data
    through the graph with zero-copy semantics where possible.

    Parameters
    ----------
    element_type : EventType
        The event type of the elements stored in the bucket.

    Notes
    -----
    The corresponding C++ event is ``tcspc::bucket<T>``, a movable handle
    to a contiguous storage region. Moving a bucket transfers ownership
    of the storage; copying allocates and copies the data.

    See Also
    --------
    :cpp:`tcspc::bucket`
        The underlying C++ bucket type.
    """

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
    """Raw 32-bit FIFO record from Becker & Hickl SPC hardware.

    Represents one record as produced by the SPC FIFO mode (does not cover
    SPC-600/630 or TDC-family devices, which use different formats).
    Typically appears upstream of `DecodeBHSPC`, which interprets the
    record and emits the appropriate detection, marker, or
    bookkeeping events.

    Notes
    -----
    The corresponding C++ event has a single field
    ``std::array<std::byte, 4> bytes`` holding the raw record.

    See Also
    --------
    :cpp:`tcspc::bh_spc_event`
        The underlying C++ event type.
    """

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::bh_spc_event")


class DataLostEvent(EventType):
    """Event indicating that the data source detected a buffer overflow.

    Emitted when an upstream data source (FIFO, DMA, or similar) detected
    that one or more events were lost before they could be delivered.
    Subsequent events may therefore be missing and the ``abstime`` field
    may have skipped time.

    Parameters
    ----------
    data_types : DataTypes or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `DataTypes` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``abstime``. The source
    should continue to produce overflow notifications for subsequent
    occurrences; cancelling the stream in response to data loss is the
    responsibility of a downstream processor.

    See Also
    --------
    :cpp:`tcspc::data_lost_event`
        The underlying C++ event type.
    """

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
    """Event representing a timing marker or external trigger.

    Used for frame, line, and pixel markers in scanning microscopy, and
    for other external trigger signals that the TCSPC hardware records
    alongside detections.

    Parameters
    ----------
    data_types : DataTypes or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `DataTypes` defaults.

    Notes
    -----
    The corresponding C++ event has fields ``abstime`` and ``channel``.
    When the hardware reports multiple simultaneous markers, one event
    is emitted per channel sharing the same ``abstime``; the ordering
    among simultaneous markers is unspecified. The marker channel
    numbering may or may not share a namespace with detection channels,
    depending on the device.

    See Also
    --------
    :cpp:`tcspc::marker_event`
        The underlying C++ event type.
    """

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
    """The canonical TCSPC detection event.

    Represents a single detection (typically a photon) carrying both a
    macrotime (``abstime``) and a microtime (``difftime``, also known as
    nanotime), along with the detector channel. This is the central event
    type processed in applications such as FLIM and FCS.

    Parameters
    ----------
    data_types : DataTypes or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `DataTypes` defaults.

    Notes
    -----
    The corresponding C++ event has fields ``abstime``, ``channel``, and
    ``difftime``.

    See Also
    --------
    :cpp:`tcspc::time_correlated_detection_event`
        The underlying C++ event type.
    """

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
    """Keep-alive event indicating that the data source has reached a given time.

    Serves two purposes: (a) propagating time progression when there are
    no detections to send (including marking the end of a measurement),
    and (b) preventing long gaps in ``abstime`` that would otherwise
    stall time-sensitive processors.

    Parameters
    ----------
    data_types : DataTypes or None
        Set of integer types parameterising the C++ event. ``None`` (the
        default) uses `DataTypes` defaults.

    Notes
    -----
    The corresponding C++ event has the field ``abstime``. The emission
    frequency of these events can be tuned on the C++ side with
    ``tcspc::regulate_time_reached()``.

    See Also
    --------
    :cpp:`tcspc::time_reached_event`
        The underlying C++ event type.
    """

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
    """Event indicating a non-fatal, recoverable issue detected by a processor.

    Used to report problems that do not by themselves require stopping
    the stream, such as input format anomalies, dropped or out-of-order
    events, or other recoverable conditions.

    Notes
    -----
    The corresponding C++ event has the field ``message``. Producers of
    warnings should also pass any received warning events through, so
    multiple warning-emitting processors can be chained ahead of a
    single handler such as `Stop` or `StopWithError`.

    See Also
    --------
    :cpp:`tcspc::warning_event`
        The underlying C++ event type.
    Stop
        Convert warnings into a normal end-of-processing.
    StopWithError
        Convert warnings into a terminating error.
    """

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::warning_event")
