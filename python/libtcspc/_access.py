# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import ABC, abstractmethod
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Protocol

from typing_extensions import override

from ._cpp_utils import (
    _CppFunctionScopeDefs,
    _CppIdentifier,
    _CppTypeName,
    _identifier_from_string,
    _ModuleCodeFragment,
)


class _AccessSpec(ABC):
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

    def _accesses(self) -> Sequence[tuple[AccessTag, "_AccessSpec"]]:
        return ()


class _AcquireAccessSpec(_AccessSpec):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::acquire_access")

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("halt"),)

    @override
    def py_class_name(self) -> str:
        return "AcquireAccess"


class _CountAccessSpec(_AccessSpec):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName("tcspc::count_access")

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("count"),)

    @override
    def py_class_name(self) -> str:
        return "CountAccess"


class _UniqueBinMapperAccessSpec(_AccessSpec):
    @override
    def _cpp_type_name(self) -> _CppTypeName:
        return _CppTypeName(
            "tcspc::unique_bin_mapper_access<"
            "tcspc::default_numeric_traits::datapoint_type>"
        )

    @override
    def cpp_methods(self) -> Sequence[_CppIdentifier]:
        return (_CppIdentifier("values"),)

    @override
    def py_class_name(self) -> str:
        return "UniqueBinMapperAccess"


class Access(Protocol):
    """
    Base protocol for run-time access objects.

    Concrete access protocols (such as `AcquireAccess` or `CountAccess`)
    derive from this. The protocol itself has no methods; it serves as a
    common type for any access object returned by
    `ExecutionContext.access`.
    """


class AcquireAccess(Access, Protocol):
    """Run-time access object for an ``Acquire`` processor."""

    def halt(self) -> None:
        """
        Request that the running acquisition stop at its earliest opportunity.
        """
        ...


class CountAccess(Access, Protocol):
    """Run-time access object for a ``Count`` processor."""

    def count(self) -> int:
        """
        Return the current event count.

        Returns
        -------
        int
            The number of events counted so far.
        """
        ...


class UniqueBinMapperAccess(Access, Protocol):
    """Run-time access object for a ``UniqueBinMapper`` bin mapper."""

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
