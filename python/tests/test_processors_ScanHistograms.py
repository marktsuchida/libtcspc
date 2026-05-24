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


def test_ScanHistograms_event_set():
    node = tcspc.ScanHistograms(4, 16, 255)
    (out,) = node._map_event_sets([(tcspc.BinIncrementClusterEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.HistogramArrayProgressEvent()._cpp_type_name() in cpp
    assert tcspc.HistogramArrayEvent()._cpp_type_name() in cpp
    assert tcspc.BinIncrementClusterEvent()._cpp_type_name() not in cpp


def test_ScanHistograms_emit_concluding():
    node = tcspc.ScanHistograms(4, 16, 255, emit_concluding=True)
    (out,) = node._map_event_sets([(tcspc.BinIncrementClusterEvent(),)])
    cpp = [e._cpp_type_name() for e in out]
    assert tcspc.ConcludingHistogramArrayEvent()._cpp_type_name() in cpp


def test_ScanHistograms_codegen():
    node = tcspc.ScanHistograms(
        4, 16, 255, reset_after_scan=True, clear_every_scan=True
    )
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::scan_histograms<(" in code
    assert "reset_after_scan" in code
    assert "clear_every_scan" in code
    assert "tcspc::arg::num_elements<std::size_t>" in code
    assert "tcspc::arg::num_bins<std::size_t>" in code
    assert "tcspc::arg::max_per_bin<" in code
    assert "DOWN" in code


def test_ScanHistograms_rejects_bad_overflow():
    with pytest.raises(ValueError):
        tcspc.ScanHistograms(4, 16, 255, overflow="bogus")
