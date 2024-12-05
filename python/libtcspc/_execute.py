# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Iterable
from contextlib import contextmanager
from typing import Any

from ._access import AccessTag
from ._compile import CompiledGraph
from ._cpp_utils import CppIdentifier


class EndOfProcessing(Exception):
    """
    Exception raised when processing finished without error, but for a reason
    other than reaching the end of the input.

    By convention, ``arg[0]`` is a message indicating the reason for stopping.
    """

    pass


class ExecutionContext:
    """
    An execution context for a compiled processing graph.

    Use `create_execution_context()` to obtain an instance. Direct
    instantiation from user code is not supported.

    This object encapsulates a single run of stream processing and cannot be
    reused.
    """

    def __init__(
        self, mod: Any, ctx: Any, proc: Any, accesses: Iterable[str]
    ) -> None:
        self._mod = mod
        self._ctx = ctx
        self._proc = proc
        self._accesses = set(accesses)
        self._end_of_life_reason: str | None = None

    def access(self, tag: str | AccessTag) -> Any:
        """
        Obtain run-time access to components of the processing graph.

        Parameters
        ----------
        tag : str | AccessTag
            The access tag.

        Returns
        -------
        Access
            The access object for the requested tag.
        """
        if isinstance(tag, AccessTag):
            tag = tag.tag
        if tag not in self._accesses:
            raise ValueError(f"no such access tag: {tag}")
        return getattr(self._ctx, f"access__{tag}")(self._proc)

    @contextmanager
    def _manage_processor_end_of_life(self):
        if self._end_of_life_reason:
            raise RuntimeError(f"processor already {self._end_of_life_reason}")
        try:
            yield
        except self._mod.EndOfProcessing as e:
            self._end_of_life_reason = "finished by detecting end of stream"
            raise EndOfProcessing(*e.args) from e
        except:
            self._end_of_life_reason = "finished with error"
            raise

    def handle(self, event: Any) -> None:
        """
        Send an event to the processor input.

        Parameters
        ----------
        event
            The event.

        Raises
        ------
        EndOfProcessing
            If the processor detected the end of the stream (of interest).
        """
        with self._manage_processor_end_of_life():
            self._proc.handle(event)

    def flush(self) -> None:
        """
        Flush the processor input.

        Raises
        ------
        EndOfProcessing
            If the processor detected the end of the stream (of interest).
        """
        with self._manage_processor_end_of_life():
            self._proc.flush()
        self._end_of_life_reason = "flushed"


def create_execution_context(
    compiled_graph: CompiledGraph,
    arguments: dict[CppIdentifier, Any] | None = None,
) -> ExecutionContext:
    """
    Create an execution context for a compiled processing graph.

    Parameters
    ----------
    compiled_graph : CompiledGraph
        The compiled graph from which to instantiate the processor.
    arguments : dict[CppIdentifier, Any]
        The values that parameters should bind to.

    Returns
    -------
    ExecutionContext
        The new execution context.
    """
    args = {} if arguments is None else arguments.copy()
    for param in compiled_graph.parameters():
        if param.name not in args.copy():
            if param.default_value is None:
                raise ValueError(
                    f"No argument given for required parameter {param.name}"
                )
            args[param.name] = param.default_value

    arg_struct = compiled_graph._mod.Params()
    for name, value in args.items():
        setattr(arg_struct, name, value)

    context = compiled_graph._mod.create_context()
    processor = compiled_graph._mod.create_processor(context, arg_struct)
    access_types = compiled_graph.accesses()

    return ExecutionContext(
        compiled_graph._mod, context, processor, access_types
    )
