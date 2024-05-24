# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import copy
import functools
import itertools
from collections.abc import Callable
from typing import Any

import cppyy

from ._graph import Graph

cppyy.include("libtcspc/tcspc.hpp")

cppyy.include("exception")
cppyy.include("memory")
cppyy.include("type_traits")


_cppyy_context_counter = itertools.count()


@functools.cache
def _instantiator(graph_code: str, context_varname: str) -> Any:
    fname = f"instantiate_graph_{next(_cppyy_context_counter)}"
    cppyy.cppdef(f"""\
        namespace tcspc::cppyy_context {{
            auto {fname}(std::shared_ptr<tcspc::context>
                         {context_varname}) {{
                return {graph_code};
            }}
        }}""")
    return getattr(cppyy.gbl.tcspc.cppyy_context, fname)


class EndOfProcessing(Exception):
    """
    Exception raised when processing finished without error, but for a reason
    other than reaching the end of the input.

    By convention, ``arg[0]`` is a message indicating the reason for stopping.
    """

    pass


class Context:
    """
    An execution context for a processing graph.

    An instance is created given a `Graph`. If the graph contains nodes that
    support name-based access, they can be accessed both before and after
    running.

    Parameters
    ----------
    graph : Graph
        The graph from which to instantiate the processor. There must be
        exactly one input port and no output ports.

    Raises
    ------
    cppyy.gbl.std.exception
        If there was an error while initializing the instantiated processing
        graph.
    """

    def __init__(self, graph: Graph) -> None:
        n_in, n_out = len(graph.inputs()), len(graph.outputs())
        if n_in != 1:
            raise ValueError(
                f"graph is not executable (must have 1 input port; found {n_in})"
            )
        if n_out > 0:
            raise ValueError(
                f"graph is not executable (must have no output ports; found {n_out})"
            )
        self._graph = copy.deepcopy(graph)
        self._ctx = cppyy.gbl.tcspc.context.create()
        ctx_var = "ctx"
        code = graph.generate_cpp("", ctx_var)
        self._proc = _instantiator(code, ctx_var)(self._ctx)
        self._spent: str | None = None  # Set to reason of expiration.

    def access(self, node_name: str) -> Any:
        """
        Obtain run-time access to an instantiated node by name.

        Parameters
        ----------
        node_name : str
            The node name.

        Returns
        -------
        Any
            The (C++) access object of the requested type.
        """

        if node_name.startswith("/"):
            node_name = node_name[1:]
        node = self._graph.node(node_name)
        access_type = node.access_type()
        if access_type is None:
            raise TypeError(f"Node {node_name} has no access type")
        return access_type(self._ctx, f"/{node_name}", ref=self._proc)

    def _check_ready(self) -> None:
        if self._spent:
            raise RuntimeError(f"processor already {self._spent}")

    def _translate_exceptions(self, func: Callable[..., None]):
        try:
            func()
        except cppyy.gbl.tcspc.end_of_processing as e:
            self._spent = "finished by detecting end of stream"
            raise EndOfProcessing(e.what()) from e
        except:
            self._spent = "finished with error"
            raise

    def handle(self, event: Any) -> None:
        """
        Send an event to the processor input.

        Parameters
        ----------
        event
            The event, which is translated to a C++ type by cppyy.

        Raises
        ------
        EndOfProcessing
            If the processor detected the end of the stream (of interest).
        cppyy.gbl.std.exception
            If there was an error during processing.
        """
        self._check_ready()
        self._translate_exceptions(lambda: self._proc.handle(event))

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
        self._check_ready()
        self._translate_exceptions(lambda: self._proc.flush())
        self._spent = "flushed"
