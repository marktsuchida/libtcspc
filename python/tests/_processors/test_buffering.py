# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._param import Param

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)
DOWN = [_CppExpression("DOWN")]


def test_ProcessInBatches_event_set_preserved():
    node = tcspc.ProcessInBatches(IntEvent, 100)
    assert node._map_event_sets([(IntEvent,)]) == ((IntEvent,),)


def test_ProcessInBatches_rejects_other_events():
    node = tcspc.ProcessInBatches(IntEvent, 100)
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent, OtherEvent)])


def test_ProcessInBatches_codegen():
    node = tcspc.ProcessInBatches(IntEvent, 100)
    code = node._cpp_expression(gencontext, DOWN)
    assert "tcspc::process_in_batches<int>(" in code
    assert "tcspc::arg::batch_size<std::size_t>" in code
    assert "DOWN" in code


def test_ProcessInBatches_param():
    node = tcspc.ProcessInBatches(IntEvent, Param("n"))
    assert len(node._parameters()) == 1
    assert "params.z_n" in node._cpp_expression(gencontext, DOWN)
