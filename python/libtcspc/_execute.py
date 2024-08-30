# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Sequence
from contextlib import contextmanager
from typing import Any

import cppyy

from ._access import Access, AccessTag
from ._compile import CompiledGraph
from ._cpp_utils import CppIdentifier

cppyy.include("libtcspc_py/handle_span.hpp")

cppyy.include("libtcspc/tcspc.hpp")


class EndOfProcessing(Exception):
    """
    Exception raised when processing finished without error, but for a reason
    other than reaching the end of the input.

    By convention, ``arg[0]`` is a message indicating the reason for stopping.
    """

    pass


class ExecutionContext:
    """
    An execution context for a processing graph.

    Use `create_execution_context()` to obtain an instance. Direct
    instantiation from user code is not supported.

    This encapsulates a single run of stream processing and cannot be reused.
    """

    def __init__(
        self,
        cpp_context,
        cpp_proc,
        access_types: Sequence[tuple[str, type[Access]]],
    ) -> None:
        self._ctx = cpp_context
        self._proc = cpp_proc
        self._access_types = dict(access_types)
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
            The access object of the requested type.
        """

        if isinstance(tag, AccessTag):
            tag = tag.tag
        access_type = self._access_types[tag]
        return access_type(self._ctx, tag, ref=self._proc)

    @contextmanager
    def _manage_processor_end_of_life(self):
        if self._end_of_life_reason:
            raise RuntimeError(f"processor already {self._end_of_life_reason}")
        try:
            yield
        except cppyy.gbl.tcspc.end_of_processing as e:
            self._end_of_life_reason = "finished by detecting end of stream"
            raise EndOfProcessing(e.what()) from e
        except:
            self._end_of_life_reason = "finished with error"
            raise

    def handle(self, event: Any) -> None:
        """
        Send an event to the processor input.

        Parameters
        ----------
        event
            The event, which is translated to a C++ type by cppyy. As a special
            case, if the event implements the buffer protocol, it is translated
            to the corresponding span.

        Raises
        ------
        EndOfProcessing
            If the processor detected the end of the stream (of interest).
        cppyy.gbl.std.exception
            If there was an error during processing.
        """
        with self._manage_processor_end_of_life():
            if cppyy.gbl.tcspc.py.is_buffer(event):
                # Explicit template argument for Proc is necessary here (cppyy
                # 3.1.2).
                cppyy.gbl.tcspc.py.handle_buffer[type(self._proc)](
                    self._proc, event
                )
            else:
                self._proc.handle(event)

    def flush(self) -> None:
        """
        Flush the processor input.

        Raises
        ------
        EndOfProcessing
            If the processor detected the end of the stream (of interest).
        cppyy.gbl.std.exception
            If there was an error during processing.
        """
        with self._manage_processor_end_of_life():
            self._proc.flush()
        self._end_of_life_reason = "flushed"


def create_execution_context(
    compiled_graph: CompiledGraph,
    arguments: dict[CppIdentifier, Any] | None = None,
) -> ExecutionContext:
    """
    Create an execution context for a compiled graph.

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

    Raises
    ------
    cppyy.gbl.std.exception
        If there was an error while initializing the instantiated processing
        graph.
    """
    args = {} if arguments is None else arguments.copy()
    for name, _, default in compiled_graph._params:
        if name not in args.copy():
            if default is None:
                raise ValueError(
                    f"No value given for required parameter {name}"
                )
            args[name] = default

    arg_struct = compiled_graph._param_struct()
    for name, value in args.items():
        setattr(arg_struct, name, value)

    context = cppyy.gbl.tcspc.context.create()
    processor = compiled_graph._instantiator(context, arg_struct)

    # handle() and flush() are wrapped by the CompiledGraph so that they
    # are overload sets, not template proxies.
    if hasattr(processor, "handle"):
        processor.handle.__release_gil__ = True
    processor.flush.__release_gil__ = True

    access_types = compiled_graph.access_types()

    return ExecutionContext(context, processor, access_types)
