# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from _test_helpers import _NamedEvent
from libtcspc._bucket_sources import RecyclingBucketSource
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _identifier_from_string,
    _size_type,
    _string_type,
    _uint64_type,
)
from libtcspc._events import BucketEvent, WarningEvent, _ByteEvent
from libtcspc._graph import Subgraph
from libtcspc._node import Node
from libtcspc._param import Param
from libtcspc._processors import (
    BatchFromBytes,
    ReadBinaryStream,
    Stop,
    StopWithError,
    UnbatchFromBytes,
    ViewAsBytes,
    read_events_from_binary_file,
)
from libtcspc._streams import BinaryFileInputStream

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


# ExtractBucket


def test_ExtractBucket_emits_bucket_of_bin_type():
    node = tcspc.ExtractBucket(tcspc.HistogramEvent())
    (out,) = node._map_event_sets([(tcspc.HistogramEvent(),)])
    assert len(out) == 1
    assert (
        out[0]._cpp_type_name()
        == "tcspc::bucket<tcspc::default_numeric_traits::bin_type>"
    )


def test_ExtractBucket_codegen():
    node = tcspc.ExtractBucket(tcspc.HistogramEvent())
    code = node._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::extract_bucket<tcspc::histogram_event<tcspc::default_numeric_traits>>("
        in code
    )
    assert "DOWN" in code


def test_ExtractBucket_rejects_non_extractable_event():
    with pytest.raises(ValueError, match="extractable"):
        tcspc.ExtractBucket(tcspc.DetectionEvent())


def test_ExtractBucket_rejects_wrong_input():
    node = tcspc.ExtractBucket(tcspc.HistogramEvent())
    with pytest.raises(ValueError):
        node._map_event_sets([(tcspc.ConcludingHistogramEvent(),)])


# ReadBinaryStream


def _read(
    max_length: int | Param[int] | None = None,
    read_granularity_bytes: int | Param[int] = 65536,
    stream: BinaryFileInputStream | None = None,
) -> ReadBinaryStream:
    return ReadBinaryStream(
        IntEvent,
        stream if stream is not None else BinaryFileInputStream("some_file"),
        max_length,
        None,
        read_granularity_bytes,
    )


def test_ReadBinaryStream_rejects_nonempty_input_set():
    node = _read()
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_ReadBinaryStream_output_event_set():
    node = _read()
    assert node._map_event_sets([()]) == (
        (BucketEvent(IntEvent), WarningEvent()),
    )


def test_ReadBinaryStream_default_max_length_is_unlimited():
    node = _read()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "std::numeric_limits<tcspc::u64>::max()" in code


def test_ReadBinaryStream_max_length_int():
    node = _read(max_length=1024)
    names = [p.name for p, _ in node._parameters()]
    assert "ml" not in names
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::u64{1024uLL}" in code


def test_ReadBinaryStream_max_length_param():
    node = _read(max_length=Param("ml"))
    params = node._parameters()
    assert (Param("ml"), _uint64_type) in params
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert f"params.{_identifier_from_string('ml')}" in code


def test_ReadBinaryStream_granularity_param():
    node = _read(read_granularity_bytes=Param("g"))
    params = node._parameters()
    assert (Param("g"), _size_type) in params
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert f"params.{_identifier_from_string('g')}" in code


def test_ReadBinaryStream_propagates_stream_parameters():
    node = _read(stream=BinaryFileInputStream(Param("fname")))
    params = node._parameters()
    assert (Param("fname"), _string_type) in params


def test_ReadBinaryStream_codegen_calls_read_binary_stream():
    node = _read()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::read_binary_stream<int>(" in code
    assert "tcspc::arg::max_length<tcspc::u64>{" in code
    assert "tcspc::arg::granularity<std::size_t>{" in code
    assert "DOWN" in code


# BatchFromBytes


def test_BatchFromBytes_accepts_bucket_of_bytes():
    node = BatchFromBytes(IntEvent)
    assert node._map_event_sets([(BucketEvent(_ByteEvent()),)]) == (
        (BucketEvent(IntEvent),),
    )


def test_BatchFromBytes_rejects_non_byte_input():
    node = BatchFromBytes(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(BucketEvent(IntEvent),)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_BatchFromBytes_codegen_calls_tcspc_batch_from_bytes():
    node = BatchFromBytes(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::batch_from_bytes<int>(" in code
    assert "DOWN" in code


def test_BatchFromBytes_default_bucket_source_is_recycling():
    node = BatchFromBytes(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "recycling_bucket_source<int," in code


def test_BatchFromBytes_buffer_provider_params_propagate():
    bp = RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc"))
    node = BatchFromBytes(IntEvent, buffer_provider=bp)
    params = node._parameters()
    assert (Param("mbc"), _size_type) in params


# UnbatchFromBytes


def test_UnbatchFromBytes_accepts_bucket_of_bytes():
    node = UnbatchFromBytes(IntEvent)
    assert node._map_event_sets([(BucketEvent(_ByteEvent()),)]) == (
        (IntEvent,),
    )


def test_UnbatchFromBytes_rejects_non_byte_input():
    node = UnbatchFromBytes(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(BucketEvent(IntEvent),)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_UnbatchFromBytes_codegen():
    node = UnbatchFromBytes(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::unbatch_from_bytes<int>(" in code
    assert "DOWN" in code


# ViewAsBytes


def test_ViewAsBytes_accepts_bucket_of_event_type():
    node = ViewAsBytes(IntEvent)
    assert node._map_event_sets([(BucketEvent(IntEvent),)]) == (
        (BucketEvent(_ByteEvent()),),
    )


def test_ViewAsBytes_rejects_mismatched_bucket():
    node = ViewAsBytes(IntEvent)
    with pytest.raises(ValueError):
        node._map_event_sets([(BucketEvent(OtherEvent),)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_ViewAsBytes_codegen():
    node = ViewAsBytes(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::view_as_bytes(" in code
    assert "DOWN" in code


# read_events_from_binary_file


def _subgraph(**kwargs) -> Subgraph:
    sg = read_events_from_binary_file(IntEvent, "f.bin", **kwargs)
    assert isinstance(sg, Subgraph)
    return sg


def test_returns_subgraph_with_input_and_output_ports():
    sg = _subgraph()
    assert sg.inputs() == ("input",)
    assert sg.outputs() == ("output",)


def test_subgraph_output_emits_element_event_type():
    sg = _subgraph()
    assert sg._map_event_sets([()]) == ((IntEvent,),)


def test_stop_normally_on_error_True_uses_Stop():
    sg = _subgraph(stop_normally_on_error=True)
    found: dict[str, bool] = {"stop": False, "stop_with_error": False}

    def visit(_: str, node: Node) -> None:
        if isinstance(node, Stop):
            found["stop"] = True
        elif isinstance(node, StopWithError):
            found["stop_with_error"] = True

    sg.graph()._visit_nodes(visit)
    assert found["stop"]
    assert not found["stop_with_error"]


def test_stop_normally_on_error_False_uses_StopWithError():
    sg = _subgraph()  # default: stop_normally_on_error=False
    found: dict[str, bool] = {"stop": False, "stop_with_error": False}

    def visit(_: str, node: Node) -> None:
        if isinstance(node, Stop):
            found["stop"] = True
        elif isinstance(node, StopWithError):
            found["stop_with_error"] = True

    sg.graph()._visit_nodes(visit)
    assert found["stop_with_error"]
    assert not found["stop"]


def test_params_propagate_through_to_parameters():
    sg = read_events_from_binary_file(
        IntEvent, Param("f"), max_length=Param("m")
    )
    names = [p.name for p, _ in sg._parameters()]
    assert "f" in names
    assert "m" in names
