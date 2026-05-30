# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import ABC, abstractmethod
from collections.abc import Sequence
from dataclasses import dataclass
from typing import TYPE_CHECKING, Protocol

from typing_extensions import override

from ._cpp_utils import (
    _CppFunctionScopeDefs,
    _CppIdentifier,
    _CppTypeName,
    _identifier_from_string,
    _ModuleCodeFragment,
)
from ._numeric_traits import NumericTraits

if TYPE_CHECKING:
    from ._events import EventInstance, EventType


class _AccessorSpec(ABC):
    @abstractmethod
    def _cpp_type_name(self) -> _CppTypeName: ...

    @abstractmethod
    def cpp_methods(self) -> Sequence[_CppIdentifier]: ...

    @abstractmethod
    def py_class_name(self) -> str: ...

    def wraps_event_value(self) -> bool:
        return False

    def cpp_bindings(self, module_var: _CppIdentifier) -> _ModuleCodeFragment:
        # The GIL is released around every accessor method. This is required
        # for the blocking buffer_accessor::pump() (which must not hold the GIL
        # while it waits and emits downstream into Python sinks); it is safe and
        # harmless for the other accessors because none of them call back into
        # Python during the C++ call (nanobind converts the return value after
        # the call guard exits, with the GIL re-held).
        py_class_name = self.py_class_name()
        return _ModuleCodeFragment(
            (),
            (),
            (),
            (
                _CppFunctionScopeDefs(
                    f'nanobind::class_<{self._cpp_type_name()}>({module_var}, "{py_class_name}", nanobind::is_final())'
                    + "".join(
                        f'\n    .def("{meth}", &{self._cpp_type_name()}::{meth}, '
                        "nanobind::call_guard<nanobind::gil_scoped_release>())"
                        for meth in self.cpp_methods()
                    )
                    + ";"
                ),
            ),
        )


@dataclass(frozen=True)
class AccessTag:
    """
    Tag attached to `_Accessible` objects.

    This tag can later be used to gain access to entities in the generated
    processor via its execution context.
    """

    tag: str

    def __post_init__(self) -> None:
        if not self.tag:
            raise ValueError("AccessTag string must not be empty")

    def _context_method_name(self) -> _CppIdentifier:
        return _CppIdentifier(f"access__{_identifier_from_string(self.tag)}")


class _Accessible(ABC):  # noqa: B024
    """
    Interface for any object that serves as a template for an entity that
    provides access through the execution context at run time.

    These are objects that constitute a ``Graph``, namely processing nodes
    (``Node``) or their auxiliary objects.
    """

    def _accesses(self) -> Sequence[tuple[AccessTag, "_AccessorSpec"]]:
        return ()


class _AcquireAccessorSpec(_AccessorSpec):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::acquire_accessor")

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("halt"),)

    @override
    def py_class_name(self) -> str:
        return "AcquireAccessor"


class _CountAccessorSpec(_AccessorSpec):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::count_accessor")

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("count"),)

    @override
    def py_class_name(self) -> str:
        return "CountAccessor"


class _UniqueBinMapperAccessorSpec(_AccessorSpec):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            "tcspc::unique_bin_mapper_accessor<"
            "tcspc::default_numeric_traits::datapoint_type>"
        )

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("values"),)

    @override
    def py_class_name(self) -> str:
        return "UniqueBinMapperAccessor"


class _RecordAbstimeRangeAccessorSpec(_AccessorSpec):
    def __init__(self, numeric_traits: NumericTraits) -> None:
        self._numeric_traits = numeric_traits

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        nt = self._numeric_traits._cpp_type_name()
        return _CppTypeName(
            f"tcspc::record_abstime_range_accessor<{nt}::abstime_type>"
        )

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("min"), _CppIdentifier("max"))

    @override
    def py_class_name(self) -> str:
        return "RecordAbstimeRangeAccessor__" + _identifier_from_string(
            str(self._cpp_type_name())
        )


class _RecordLastAccessorSpec(_AccessorSpec):
    def __init__(self, event_type: "EventType") -> None:
        self._event_type = event_type

    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            f"tcspc::record_last_accessor<{self._event_type._cpp_type_name()}>"
        )

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("get"),)

    @override
    def py_class_name(self) -> str:
        return "RecordLastAccessor__" + _identifier_from_string(
            str(self._event_type._cpp_type_name())
        )

    @override
    def wraps_event_value(self) -> bool:
        return True


class _BufferAccessorSpec(_AccessorSpec):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::buffer_accessor")

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("pump"), _CppIdentifier("halt"))

    @override
    def py_class_name(self) -> str:
        return "BufferAccessor"


class Accessor(Protocol):
    """
    Base protocol for run-time accessors.

    Concrete accessor protocols (such as `AcquireAccessor` or `CountAccessor`)
    derive from this. The protocol itself has no methods; it serves as a
    common type for any accessor returned by
    `ExecutionContext.access`.
    """


class AcquireAccessor(Accessor, Protocol):
    """Run-time accessor for an ``Acquire`` processor."""

    def halt(self) -> None:
        """
        Request that the running acquisition stop at its earliest opportunity.
        """
        ...


class CountAccessor(Accessor, Protocol):
    """Run-time accessor for a ``Count`` processor."""

    def count(self) -> int:
        """
        Return the current event count.

        Returns
        -------
        int
            The number of events counted so far.
        """
        ...


class UniqueBinMapperAccessor(Accessor, Protocol):
    """Run-time accessor for a ``UniqueBinMapper`` bin mapper."""

    def values(self) -> list[int]:
        """
        Return the datapoint values assigned to bin indices.

        Returns
        -------
        list[int]
            The datapoint value assigned to each bin index, in bin-index
            order.
        """
        ...


class RecordAbstimeRangeAccessor(Accessor, Protocol):
    """Run-time accessor for a ``RecordAbstimeRange`` processor."""

    def min(self) -> int | None:
        """
        Return the minimum abstime observed so far.

        Returns
        -------
        int or None
            The smallest ``abstime`` of any abstime-stamped event observed,
            or ``None`` if no such event has been observed yet.
        """
        ...

    def max(self) -> int | None:
        """
        Return the maximum abstime observed so far.

        Returns
        -------
        int or None
            The largest ``abstime`` of any abstime-stamped event observed,
            or ``None`` if no such event has been observed yet.
        """
        ...


class RecordLastAccessor(Accessor, Protocol):
    """Run-time accessor for a ``RecordLast`` processor."""

    def get(self) -> "EventInstance | None":
        """
        Return the last recorded event.

        Returns
        -------
        EventInstance or None
            A copy of the last event of the recorded type that passed
            through, or ``None`` if no such event has been observed yet.
        """
        ...


class BufferAccessor(Accessor, Protocol):
    """Run-time accessor for a ``Buffer`` or ``RealTimeBuffer`` processor.

    A buffer splits the processing graph into a *producer half* (the
    processors upstream of the buffer, driven by `ExecutionContext.handle` /
    `ExecutionContext.flush`) and a *consumer half* (the processors downstream
    of the buffer). Events enqueued by the producer are drained and emitted on
    a separate *pump thread* that the application must run by calling `pump`.

    Because of this, `ExecutionContext.flush` returns as soon as the producer
    half is flushed; it does *not* wait for the consumer half to drain.
    Processing is complete only when the pump thread (the `pump` call) has
    returned. The application is responsible for spawning and joining the pump
    thread, for catching the exception that `pump` may raise, and for keeping
    the `ExecutionContext` alive until the pump thread has finished (the
    accessor holds a raw pointer into the processor).
    """

    def pump(self) -> None:
        """
        Drain buffered events and emit them downstream on the calling thread.

        This call **blocks** and must be made on a thread other than the one
        calling `ExecutionContext.handle` / `ExecutionContext.flush`. The GIL
        is released while it blocks and drains.

        Returns
        -------
        None
            Returns normally when the producer half flushes and the queue
            drains.

        Raises
        ------
        SourceHalted
            If `halt` was called before the buffer received a flush from its
            producer half.
        EndOfProcessing
            If a downstream sink terminated processing.
        Exception
            Any other exception raised by a downstream processor (e.g. a sink
            raising an error). After such a downstream termination, the
            producer thread's next `ExecutionContext.handle` /
            `ExecutionContext.flush` raises `EndOfProcessing`.
        """
        ...

    def halt(self) -> None:
        """
        Signal that the producer stopped without flushing.

        Causes a blocked or subsequent `pump` to raise `SourceHalted` rather
        than returning normally. Must be called if the source stops (e.g. on
        cancellation or a producer-side error) without flushing the producer
        half, so that the pump thread does not block forever. It is a harmless
        no-op if the buffer has already flushed or otherwise terminated.
        """
        ...
