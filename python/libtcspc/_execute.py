# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from contextlib import contextmanager
from typing import Any

import cppyy

from ._access import AccessTag
from ._compile import CompiledGraph

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

    This encapsulates a single run of stream processing.

    Parameters
    ----------
    compiled_graph : CompiledGraph
        The compiled graph from which to instantiate the processor.

    Raises
    ------
    cppyy.gbl.std.exception
        If there was an error while initializing the instantiated processing
        graph.
    """

    def __init__(self, compiled_graph: CompiledGraph) -> None:
        self._ctx = cppyy.gbl.tcspc.context.create()
        self._end_of_life_reason: str | None = None

        self._proc = compiled_graph._instantiate(self._ctx)
        # handle() and flush() are wrapped by the CompiledGraph so that they
        # are overload sets, not template proxies.
        if hasattr(self._proc, "handle"):
            self._proc.handle.__release_gil__ = True
        self._proc.flush.__release_gil__ = True

        self._access_types = compiled_graph.access_types()

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
