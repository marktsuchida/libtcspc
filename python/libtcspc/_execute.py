# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from abc import ABC, abstractmethod
from collections.abc import Mapping, Sequence
from contextlib import contextmanager
from typing import Any, final

from typing_extensions import override

from ._access import AccessTag, _AccessorSpec, _BufferAccessorSpec
from ._compile import CompiledGraph
from ._cpp_utils import _CppIdentifier
from ._events import EventInstance, EventType, _ArrayField, _Field


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
            The event object. The event type declared on the graph's output
            edge must be one that is explicitly supported for delivery to
            Python; graphs whose output carries an unsupported event type are
            rejected when the graph is compiled. Bare bucket events
            (`BucketEvent`, `ConstBucketEvent`) are delivered as NumPy
            arrays; events with fields, including bucket-carrying events such
            as `HistogramEvent`, are delivered as `EventInstance` values
            whose bucket fields read as read-only NumPy arrays.

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


def _wrapper_to_event_instance(
    wrapper: Any, wrappers: dict[type, tuple[EventType, list[_Field]]]
) -> EventInstance:
    # Convert a generated event-wrapper instance delivered by the compiled
    # module into an EventInstance, using the wrapper -> (EventType, field
    # schema) correspondence. The wrapper's type must be present in 'wrappers'.
    # The trusted path is required to preserve zero-copy array delivery.
    # Array-event fields recurse: each element wrapper is itself converted.
    event_type, schema = wrappers[type(wrapper)]
    fields: dict[str, Any] = {}
    for f in schema:
        raw = getattr(wrapper, f.name)
        if isinstance(f, _ArrayField):
            fields[f.name] = tuple(
                _wrapper_to_event_instance(e, wrappers) for e in raw
            )
        else:
            fields[f.name] = raw
    return EventInstance._from_wrapper(event_type, fields)


def _event_instance_to_wrapper(
    inst: EventInstance, wrapper_by_cpp: dict[str, tuple[type, list[_Field]]]
) -> Any:
    # Build a generated event-wrapper instance from an EventInstance, using the
    # C++ type-name -> (wrapper pyclass, field schema) correspondence.
    # Array-event fields recurse: each element EventInstance becomes an element
    # wrapper. The event type (and, for arrays, the element types) must be
    # present in 'wrapper_by_cpp'.
    cpp = str(inst._event_type._cpp_type_name())
    pyclass, schema = wrapper_by_cpp[cpp]
    wrapper = pyclass()
    for f in schema:
        field_value = inst._fields[f.name]
        if isinstance(f, _ArrayField):
            assert isinstance(field_value, tuple)
            setattr(
                wrapper,
                f.name,
                [
                    _event_instance_to_wrapper(e, wrapper_by_cpp)
                    for e in field_value
                ],
            )
        else:
            setattr(wrapper, f.name, field_value)
    return wrapper


@final
class _RecordLastAccessor:
    # Wraps the raw nanobind record_last_accessor so that get() yields an
    # EventInstance (or None) instead of the generated wrapper class. Reuses
    # the same wrapper -> (EventType, field names) map as the sink path. The
    # raw accessor also keeps the processor alive, so retaining it here is
    # sufficient.
    def __init__(
        self, raw: Any, wrappers: dict[type, tuple[EventType, list[_Field]]]
    ) -> None:
        self._raw = raw
        self._wrappers = wrappers

    def get(self) -> EventInstance | None:
        w = self._raw.get()
        if w is None:
            return None
        return _wrapper_to_event_instance(w, self._wrappers)


class EndOfProcessing(Exception):
    """
    Exception raised when processing finished without error, but for a reason
    other than reaching the end of the input.

    By convention, ``args[0]`` is a message indicating the reason for stopping.
    """

    pass


class SourceHalted(Exception):
    """
    Exception raised by `BufferAccessor.pump` when the buffer was halted.

    A pump thread observes this (instead of returning normally) when
    `BufferAccessor.halt` was called before the buffer's producer half was
    flushed -- that is, when the source stopped without flushing. It indicates
    a clean (non-error) early termination of the consumer half.
    """

    pass


@final
class _BufferAccessor:
    # Wraps the raw nanobind buffer_accessor so that pump() raises the
    # importable, module-independent EndOfProcessing / SourceHalted (each
    # compiled module defines its own nanobind exception types, since each uses
    # a distinct NB_DOMAIN). halt() needs no translation. The raw accessor
    # keeps the processor alive, so retaining it here is sufficient.
    def __init__(self, raw: Any, mod: Any) -> None:
        self._raw = raw
        self._mod = mod

    def pump(self) -> None:
        try:
            self._raw.pump()
        except self._mod.EndOfProcessing as e:
            raise EndOfProcessing(*e.args) from e
        except self._mod.SourceHalted as e:
            raise SourceHalted(*e.args) from e

    def halt(self) -> None:
        self._raw.halt()


def _build_execution(
    compiled_graph: CompiledGraph,
    arguments: dict[str, Any] | None,
    downstreams: Sequence[PySink] | None,
) -> tuple[
    Any,
    Any,
    Any,
    set[str],
    dict[str, tuple[type, list[_Field]]],
    dict[type, tuple[EventType, list[_Field]]],
    Mapping[str, _AccessorSpec],
]:
    given_args = {} if arguments is None else arguments.copy()
    encoders = compiled_graph._param_encoders
    args: dict[_CppIdentifier, Any] = {}
    for param in compiled_graph.parameters():
        if param.name in given_args:
            raw = given_args.pop(param.name)
        elif param.default_value is None:
            raise ValueError(
                f"No argument given for required parameter {param.name}"
            )
        else:
            raw = param.default_value
        enc = encoders.get(param.name)
        value = enc(raw) if enc is not None else raw
        if isinstance(value, EventInstance):
            value = _event_instance_to_wrapper(
                value, compiled_graph._wrapper_by_cpp
            )
        args[param._cpp_identifier()] = value
    for name, _ in given_args.items():
        raise ValueError(f"Unknown argument: {name}")

    arg_struct = compiled_graph._mod.Params()
    for cpp_identifier, value in args.items():
        setattr(arg_struct, cpp_identifier, value)

    wrapper_to_event = compiled_graph._eventtype_by_wrapper

    # Bridge from our common PySink to the compiled module's PySink:
    @final
    class PySinkAdapter(compiled_graph._mod.PySink):  # type: ignore
        def __init__(self, sink: PySink):
            super().__init__()  # This is required!
            self._sink = sink

        @override
        def handle(self, event):
            if type(event) in wrapper_to_event:
                event = _wrapper_to_event_instance(event, wrapper_to_event)
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

    return (
        compiled_graph._mod,
        context,
        processor,
        accesses,
        compiled_graph._wrapper_by_cpp,
        compiled_graph._eventtype_by_wrapper,
        compiled_graph._accessor_specs,
    )


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
        (
            self._mod,
            self._ctx,
            self._proc,
            self._accesses,
            self._input_wrappers,
            self._output_wrappers,
            self._accessor_specs,
        ) = _build_execution(compiled_graph, arguments, downstreams)
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
        Accessor
            The accessor for the requested tag. The concrete protocol
            (e.g., `AcquireAccessor`, `CountAccessor`) depends on the node type
            that was tagged.
        """
        if tag.tag not in self._accesses:
            raise ValueError(f"no such access tag: {tag.tag}")
        raw = getattr(self._ctx, tag._context_method_name())(self._proc)
        spec = self._accessor_specs[tag.tag]
        if spec.wraps_event_value():
            return _RecordLastAccessor(raw, self._output_wrappers)
        if isinstance(spec, _BufferAccessorSpec):
            return _BufferAccessor(raw, self._mod)
        return raw

    def cpp_to_graphviz(self) -> str:
        """
        Return a Graphviz dot representation of the compiled C++ processor graph.

        Unlike `Graph.to_graphviz`, which describes the Python-side graph before
        compilation, this method introspects the actual C++ processor graph that
        was instantiated by code generation. Convenience helpers (such as
        `read_events_from_binary_file`) and `Subgraph` nodes are fully expanded.

        The output is intended for debugging and visualization only. **The exact
        format is not stable** and should not be consumed programmatically.

        Returns
        -------
        str
            A complete ``digraph G { ... }`` block in Graphviz DOT format.

        Notes
        -----
        Safe to call at any point during the context's lifetime: before
        `handle()`, between calls, or after `flush()` / `EndOfProcessing`. The
        method does not interact with the processor's end-of-life state
        (consistent with `access()`).
        """
        return self._proc._graphviz()

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
        if isinstance(event, EventInstance):
            cpp = str(event._event_type._cpp_type_name())
            if cpp not in self._input_wrappers:
                raise TypeError(
                    f"event type {type(event._event_type).__name__} is not an "
                    "accepted input event type for this graph"
                )
            event = _event_instance_to_wrapper(event, self._input_wrappers)
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
