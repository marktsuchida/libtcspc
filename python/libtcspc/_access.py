# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence
from dataclasses import dataclass

import cppyy
from typing_extensions import override

from ._cpp_utils import CppTypeName

cppyy.include("stdexcept")


class Access:
    def __init__(self, cpp_ctx, name: str, ref: object) -> None:
        self._ref = ref  # Extend lifetime
        try:
            self._access = cpp_ctx.access[self.cpp_type_name()](name)
        except cppyy.gbl.std.range_error as e:
            raise LookupError(f"Access for node {name} does not exist") from e
        except cppyy.gbl.std.bad_any_cast as e:
            raise TypeError(
                f"Access for node {name} exists but does not have type {type}"
            ) from e

    def cpp_type_name(self) -> CppTypeName:
        raise NotImplementedError()


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

    def accesses(self) -> Sequence[tuple[str, type[Access]]]:
        """
        Return the names and types of accesses offered by this object and any
        sub-objects.

        Returns
        -------
        Sequence[tuple[str, type]]
            Access tags and their (Python) types.
        """
        return ()


class CountAccess(Access):
    @override
    def cpp_type_name(self) -> CppTypeName:
        return CppTypeName("tcspc::count_access")

    def count(self) -> int:
        return self._access.count()
