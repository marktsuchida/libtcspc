# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier
from libtcspc._events import _TraitsMemberEvent

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


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
