# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName

StartEvent = _NamedEvent(_CppTypeName("long"))
StopEvent = _NamedEvent(_CppTypeName("short"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_ClusterBinIncrements_event_set():
    node = tcspc.ClusterBinIncrements(StartEvent, StopEvent)
    (out,) = node._map_event_sets(
        [(tcspc.BinIncrementEvent(), StartEvent, StopEvent)]
    )
    assert out == (tcspc.BinIncrementClusterEvent(),)


def test_ClusterBinIncrements_passes_others():
    other = _NamedEvent(_CppTypeName("char"))
    node = tcspc.ClusterBinIncrements(StartEvent, StopEvent)
    (out,) = node._map_event_sets(
        [(tcspc.BinIncrementEvent(), StartEvent, StopEvent, other)]
    )
    cpp = [e._cpp_type_name() for e in out]
    assert other._cpp_type_name() in cpp
    assert tcspc.BinIncrementClusterEvent()._cpp_type_name() in cpp


def test_ClusterBinIncrements_codegen():
    node = tcspc.ClusterBinIncrements(StartEvent, StopEvent)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::cluster_bin_increments<" in code
    assert "long" in code and "short" in code
    assert "DOWN" in code
