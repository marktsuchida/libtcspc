# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier
from libtcspc._param import Param

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


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
