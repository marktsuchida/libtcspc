# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence
from dataclasses import dataclass
from typing import Generic, TypeVar

from libtcspc._cpp_utils import CppIdentifier, CppTypeName

T = TypeVar("T")


@dataclass(frozen=True)
class Param(Generic[T]):
    """
    Placeholder for a run-time parameter in a processing graph.
    """

    name: CppIdentifier
    default_value: T | None = None


class Parameterized:
    """
    Interface for any object that depends on parameters settable at run time.

    These are objects that constitue a ``Graph``, namely processing nodes
    (``Node``) or their auxiliary objects.

    The parameters are left unbound at compile time; values need to be supplied
    when creating an execution context from a compiled graph.
    """

    def parameters(self) -> Sequence[tuple[Param, CppTypeName]]:
        """
        Return the names, C++ types, and default values of the parameters of
        this object and any sub-objects.

        Returns
        -------
        Sequence[tuple[Param, CppTypeName]]
            Parameters (name and optional default value) and their C++ types.
        """
        return ()
