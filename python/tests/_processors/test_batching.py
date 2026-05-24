# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from typing import final

import libtcspc as tcspc
import pytest
from _test_helpers import _NamedEvent
from libtcspc._bucket_sources import RecyclingBucketSource
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._compile import CompiledGraph
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _identifier_from_string,
    _size_type,
)
from libtcspc._events import BucketEvent, _TraitsMemberEvent
from libtcspc._execute import ExecutionContext, PySink
from libtcspc._graph import Graph
from libtcspc._param import Param
from libtcspc._processors import Batch, Unbatch
from typing_extensions import override

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)

DOWN = [_CppExpression("DOWN")]


@final
class _MockSink(PySink):
    def __init__(self, log: list[str]) -> None:
        self._log = log

    @override
    def handle(self, event) -> None:
        self._log.append(f"handle({event})")

    @override
    def flush(self) -> None:
        self._log.append("flush()")


def test_Batch_accepts_only_matching_event():
    node = Batch(IntEvent)
    assert node._map_event_sets([(IntEvent,)]) == ((BucketEvent(IntEvent),),)
    with pytest.raises(ValueError):
        node._map_event_sets([(OtherEvent,)])


def test_Batch_default_batch_size_is_65536():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "std::size_t{65536uLL}" in code


def test_Batch_batch_size_int():
    node = Batch(IntEvent, batch_size=42)
    assert len(node._parameters()) == 0
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "std::size_t{42uLL}" in code


def test_Batch_batch_size_param():
    node = Batch(IntEvent, batch_size=Param("bs"))
    params = node._parameters()
    assert len(params) == 1
    assert params[0] == (Param("bs"), _size_type)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert f"params.{_identifier_from_string('bs')}" in code


def test_Batch_buffer_provider_params_propagate():
    bp = RecyclingBucketSource(IntEvent, max_bucket_count=Param("mbc"))
    node = Batch(IntEvent, buffer_provider=bp)
    params = node._parameters()
    assert (Param("mbc"), _size_type) in params


def test_Batch_default_bucket_source_is_recycling():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "recycling_bucket_source<int," in code


def test_Batch_codegen_calls_tcspc_batch():
    node = Batch(IntEvent)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::batch<int>(" in code
    assert "DOWN" in code


def test_Batch_param_round_trip():
    g = Graph()
    g.add_node("b", Batch(IntEvent, batch_size=Param("bs")))
    cg = CompiledGraph(g, (IntEvent,))
    log: list[str] = []
    c = ExecutionContext(cg, {"bs": 2}, (_MockSink(log),))
    c.handle(10)
    assert log == []
    c.handle(20)
    assert log == ["handle([10 20])"]


def test_BatchBinIncrementClusters_event_set():
    node = tcspc.BatchBinIncrementClusters(1024, 10)
    (out,) = node._map_event_sets([(tcspc.BinIncrementClusterEvent(),)])
    assert len(out) == 1
    assert (
        out[0]._cpp_type_name()
        == "tcspc::bucket<tcspc::default_numeric_traits::bin_index_type>"
    )


def test_BatchBinIncrementClusters_codegen():
    node = tcspc.BatchBinIncrementClusters(1024, 10)
    code = node._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::batch_bin_increment_clusters<tcspc::default_numeric_traits>("
        in code
    )
    assert "tcspc::arg::bucket_size<std::size_t>" in code
    assert "tcspc::arg::batch_size<std::size_t>" in code
    assert "DOWN" in code


def test_BatchBinIncrementClusters_params():
    node = tcspc.BatchBinIncrementClusters(Param("bk"), Param("ba"))
    assert len(node._parameters()) == 2


def test_Unbatch_accepts_only_matching_bucket_event():
    node = Unbatch(BucketEvent(IntEvent))
    assert node._map_event_sets([(BucketEvent(IntEvent),)]) == ((IntEvent,),)
    with pytest.raises(ValueError):
        node._map_event_sets([(BucketEvent(OtherEvent),)])
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_Unbatch_codegen_calls_tcspc_unbatch():
    node = Unbatch(BucketEvent(IntEvent))
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::unbatch<tcspc::bucket<int>>(" in code
    assert "DOWN" in code


def _bucket_input():
    return tcspc.BucketEvent(
        _TraitsMemberEvent(tcspc.NumericTraits(), "bin_index_type")
    )


def test_UnbatchBinIncrementClusters_event_set():
    node = tcspc.UnbatchBinIncrementClusters()
    (out,) = node._map_event_sets([(_bucket_input(),)])
    assert out == (tcspc.BinIncrementClusterEvent(),)


def test_UnbatchBinIncrementClusters_codegen():
    node = tcspc.UnbatchBinIncrementClusters()
    code = node._cpp_expression(gencontext, DOWN)
    assert (
        "tcspc::unbatch_bin_increment_clusters<tcspc::default_numeric_traits>("
        in code
    )
    assert "DOWN" in code
