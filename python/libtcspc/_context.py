# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import copy
import functools
import itertools
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
            auto {fname}(std::shared_ptr<tcspc::processor_context>
                         {context_varname}) {{
                return {graph_code};
            }}
        }}""")
    return getattr(cppyy.gbl.tcspc.cppyy_context, fname)


class EndProcessing(Exception):
    """
    Exception raised when processing finished without error, but for a reason
    other than reaching the end of the input.

    By convention, ``arg[0]`` is a message indicating the reason for stopping.
    """

    pass


class ProcessorContext:
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
        self._ctx = cppyy.gbl.std.make_shared["tcspc::processor_context"]()
        ctx_var = "ctx"
        code = graph.generate_cpp("", ctx_var)
        self._proc = _instantiator(code, ctx_var)(self._ctx)
        self._flushable = True

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

    # TODO handle(self, event) (with appropriate arrangement for input event
    # types from Python)

    def flush(self) -> None:
        """
        Flush the processor input.

        Raises
        ------
        EndProcessing
            If processing finished without error, but for a reason other than
            the end of the input being reached.
        cppyy.gbl.std.exception
            If there was an error during processing.
        """
        if not self._flushable:
            raise RuntimeError("already flushed")
        self._flushable = False
        try:
            self._proc.flush()
        except cppyy.gbl.tcspc.end_processing as e:
            raise EndProcessing(e.what()) from e
