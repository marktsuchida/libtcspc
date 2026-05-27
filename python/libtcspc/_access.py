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
        py_class_name = self.py_class_name()
        return _ModuleCodeFragment(
            (),
            (),
            (),
            (
                _CppFunctionScopeDefs(
                    f'nanobind::class_<{self._cpp_type_name()}>({module_var}, "{py_class_name}", nanobind::is_final())'
                    + "".join(
                        f'\n    .def("{meth}", &{self._cpp_type_name()}::{meth})'
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
