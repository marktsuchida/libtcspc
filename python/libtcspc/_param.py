# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import ABC
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Generic, TypeVar

from libtcspc._cpp_utils import (
    _CppIdentifier,
    _CppTypeName,
    _identifier_from_string,
)

T = TypeVar("T")


@dataclass(frozen=True)
class Param(Generic[T]):
    """Typed placeholder for a run-time parameter of a processing graph.

    Processing nodes, input streams, bucket sources, and acquisition
    readers accept `Param` instances in place of concrete values at
    graph-build time. At that point a `Param` only declares a named,
    typed slot to be filled in later; codegen substitutes a reference
    to a generated parameter struct. At execution time
    `ExecutionContext` binds each name to a concrete value supplied by
    the caller, falling back to ``default_value`` when the caller did
    not provide one.

    The generic parameter ``T`` is the Python-side type of the value
    (for example, ``int`` or ``str``). The C++ type used in the
    generated source is determined separately by the processor that
    declares the parameter.

    Parameters
    ----------
    name : str
        Name used to bind the parameter at run time. Must be unique
        within a graph.
    default_value : T or None
        Value used when no argument is supplied at execution time.
        ``None`` (the default) makes the parameter required.

    See Also
    --------
    ExecutionContext
        Binds `Param` placeholders to concrete values at run time.
    """

    name: str
    default_value: T | None = None

    def __post_init__(self) -> None:
        if not self.name:
            raise ValueError("Param name must not be empty")

    def _cpp_identifier(self) -> _CppIdentifier:
        return _identifier_from_string(self.name)


class _Parameterized(ABC):  # noqa: B024
    """
    Interface for any object that depends on parameters settable at run time.

    These are objects that constitute a ``Graph``, namely processing nodes
    (``Node``) or their auxiliary objects.

    The parameters are left unbound at compile time; values need to be supplied
    when creating an execution context from a compiled graph.
    """

    def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
        """
        Return the names, C++ types, and default values of the parameters of
        this object and any sub-objects.

        Returns
        -------
        Sequence[tuple[Param, _CppTypeName]]
            Parameters (name and optional default value) and their C++ types.
        """
        return ()
