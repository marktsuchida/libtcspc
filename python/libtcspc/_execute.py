# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import ABC, abstractmethod
from collections.abc import Sequence
from contextlib import contextmanager
from typing import Any, final

from typing_extensions import override

from ._access import AccessTag
from ._compile import CompiledGraph
from ._cpp_utils import _CppIdentifier


class PySink(ABC):
    """
    Abstract base class for sinks (output handling processors) written in
    Python.
    """

    @abstractmethod
    def handle(self, event: Any) -> None:
        """
        Process one event emitted to this sink from the graph.

        Parameters
        ----------
        event
            The event object. The Python type depends on the event type
            declared on the corresponding graph output: a wrapped event
            for most types, or a NumPy array for `BucketEvent`.

        Notes
        -----
        Called once per event delivered to this sink. Exceptions raised
        propagate out of the surrounding `ExecutionContext.handle` call.
        """
        ...

    @abstractmethod
    def flush(self) -> None:
        """
        Signal end of input to this sink.

        Notes
        -----
        Called once after the graph's downstream output is flushed.
        Exceptions raised propagate out of the surrounding
        `ExecutionContext.flush` call.
        """
        ...


class EndOfProcessing(Exception):
    """
    Exception raised when processing finished without error, but for a reason
    other than reaching the end of the input.

    By convention, ``args[0]`` is a message indicating the reason for stopping.
    """

    pass


def _build_execution(
    compiled_graph: CompiledGraph,
    arguments: dict[str, Any] | None,
    downstreams: Sequence[PySink] | None,
) -> tuple[Any, Any, Any, set[str]]:
    given_args = {} if arguments is None else arguments.copy()
    args: dict[_CppIdentifier, Any] = {}
    for param in compiled_graph.parameters():
        if param.name in given_args:
            args[param._cpp_identifier()] = given_args.pop(param.name)
        else:
            if param.default_value is None:
                raise ValueError(
                    f"No argument given for required parameter {param.name}"
                )
            args[param._cpp_identifier()] = param.default_value
    for name, _ in given_args.items():
        raise ValueError(f"Unknown argument: {name}")

    arg_struct = compiled_graph._mod.Params()
    for cpp_identifier, value in args.items():
        setattr(arg_struct, cpp_identifier, value)

    # Bridge from our common PySink to the compiled module's PySink:
    @final
    class PySinkAdapter(compiled_graph._mod.PySink):  # type: ignore
        def __init__(self, sink: PySink):
            super().__init__()  # This is required!
            self._sink = sink

        @override
        def handle(self, event):
            self._sink.handle(event)

        @override
        def flush(self):
            self._sink.flush()

    sinks = tuple(
        PySinkAdapter(ds)
        for ds in (() if downstreams is None else downstreams)
    )

    context = compiled_graph._mod.create_context()
    processor = compiled_graph._mod.create_processor(
        context, arg_struct, sinks
    )
    accesses = set(tag.tag for tag in compiled_graph._accesses())

    return compiled_graph._mod, context, processor, accesses


class ExecutionContext:
    """
    An execution context for a compiled processing graph.

    This object encapsulates a single run of stream processing and cannot be
    reused.

    Parameters
    ----------
    compiled_graph : CompiledGraph
        The compiled graph from which to instantiate the processor.
    arguments : dict[str, Any] or None
        The values that parameters should bind to, keyed by `Param` name.
        ``None`` (the default) means all parameters use their defaults.
    """

    def __init__(
        self,
        compiled_graph: CompiledGraph,
        arguments: dict[str, Any] | None = None,
        downstreams: Sequence[PySink] | None = None,
    ) -> None:
        self._mod, self._ctx, self._proc, self._accesses = _build_execution(
            compiled_graph, arguments, downstreams
        )
        self._end_of_life_reason: str | None = None

    def access(self, tag: AccessTag) -> Any:
        """
        Obtain run-time access to components of the processing graph.

        Parameters
        ----------
        tag : AccessTag
            The access tag.

        Returns
        -------
        Access
            The access object for the requested tag. The concrete protocol
            (e.g., `AcquireAccess`, `CountAccess`) depends on the node type
            that was tagged.
        """
        if tag.tag not in self._accesses:
            raise ValueError(f"no such access tag: {tag.tag}")
        return getattr(self._ctx, tag._context_method_name())(self._proc)

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
