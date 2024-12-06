# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

__all__ = [
    "Access",
    "AccessTag",
    "Accessible",
    "CountAccess",
]

from collections.abc import Sequence
from dataclasses import dataclass

from typing_extensions import override

from ._cpp_utils import (
    CppFunctionScopeDefs,
    CppIdentifier,
    CppNamespaceScopeDefs,
    CppTypeName,
    ModuleCodeFragment,
)


class Access:
    @classmethod
    def cpp_type_name(cls) -> CppTypeName:
        raise NotImplementedError()

    @classmethod
    def cpp_methods(cls) -> Sequence[CppIdentifier]:
        raise NotImplementedError()

    @classmethod
    def cpp_bindings(cls, module_var: CppIdentifier) -> ModuleCodeFragment:
        py_class_name = cls.__name__  # Use same name as codegen class.
        return ModuleCodeFragment(
            (),
            (),
            CppNamespaceScopeDefs(""),
            CppFunctionScopeDefs(
                f'nanobind::class_<{cls.cpp_type_name()}>({module_var}, "{py_class_name}")'
                + "".join(
                    f'\n    .def("{meth}", &{cls.cpp_type_name()}::{meth})'
                    for meth in cls.cpp_methods()
                )
                + ";"
            ),
        )


@dataclass(frozen=True)
class AccessTag:
    """
    Tag attached to Accessible objects.

    This tag can later be used to gain access to entities in the generated
    processor via its execution context.
    """

    tag: str


class Accessible:
    """
    Interface for any object that serves as a template for an entity that
    provides access through the execution context at run time.

    These are objects that constitute a ``Graph``, namely processing nodes
    (``Node``) or their auxiliary objects.
    """

    def accesses(self) -> Sequence[tuple[AccessTag, type[Access]]]:
        """
        Return the names and types of accesses offered by this object and any
        sub-objects.

        Returns
        -------
        Sequence[tuple[AccessTag, type]]
            Access tags and their (Python) types.
        """
        return ()


class CountAccess(Access):
    @override
    @classmethod
    def cpp_type_name(cls) -> CppTypeName:
        return CppTypeName("tcspc::count_access")

    @override
    @classmethod
    def cpp_methods(cls) -> Sequence[CppIdentifier]:
        return (CppIdentifier("count"),)
