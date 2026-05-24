# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


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
