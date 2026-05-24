# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._events import (
    BeginLostIntervalEvent,
    BHSPC600_256chEvent,
    BHSPC600_4096chEvent,
    BHSPCEvent,
    BulkCountsEvent,
    DataLostEvent,
    DetectionEvent,
    EndLostIntervalEvent,
    LostCountsEvent,
    MarkerEvent,
    PQT2GenericEvent,
    PQT2HydraHarpV1Event,
    PQT2PicoHarp300Event,
    PQT3GenericEvent,
    PQT3HydraHarpV1Event,
    PQT3PicoHarp300Event,
    SwabianTagEvent,
    TimeCorrelatedDetectionEvent,
    TimeReachedEvent,
    WarningEvent,
)
from libtcspc._numeric_traits import NumericTraits
from libtcspc._processors import (
    DecodeBHSPC,
    DecodeBHSPC600_256ch,
    DecodeBHSPC600_4096ch,
    DecodeBHSPCWithIntensityCounter,
    DecodePQT2Generic,
    DecodePQT2HydraHarpV1,
    DecodePQT2PicoHarp300,
    DecodePQT3Generic,
    DecodePQT3HydraHarpV1,
    DecodePQT3PicoHarp300,
    DecodeSwabianTags,
)

IntEvent = _NamedEvent(_CppTypeName("int"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_DecodeBHSPC_accepts_only_BHSPCEvent():
    node = DecodeBHSPC()
    out = node._map_event_sets([(BHSPCEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeBHSPC_output_event_set():
    dt = NumericTraits()
    node = DecodeBHSPC(dt)
    assert node._map_event_sets([(BHSPCEvent(),)]) == (
        (
            DataLostEvent(dt),
            MarkerEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeBHSPC_codegen_calls_decode_bh_spc():
    dt = NumericTraits()
    node = DecodeBHSPC(dt)
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_bh_spc<" in code
    assert dt._cpp_type_name() in code
    assert "DOWN" in code


def test_DecodeBHSPC600_256ch_accepts_only_BHSPC600_256chEvent():
    node = DecodeBHSPC600_256ch()
    out = node._map_event_sets([(BHSPC600_256chEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeBHSPC600_256ch_output_event_set():
    dt = NumericTraits()
    node = DecodeBHSPC600_256ch(dt)
    assert node._map_event_sets([(BHSPC600_256chEvent(),)]) == (
        (
            DataLostEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeBHSPC600_256ch_codegen():
    node = DecodeBHSPC600_256ch()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_bh_spc600_256ch<" in code
    assert "DOWN" in code


def test_DecodeBHSPC600_4096ch_accepts_only_BHSPC600_4096chEvent():
    node = DecodeBHSPC600_4096ch()
    out = node._map_event_sets([(BHSPC600_4096chEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeBHSPC600_4096ch_output_event_set():
    dt = NumericTraits()
    node = DecodeBHSPC600_4096ch(dt)
    assert node._map_event_sets([(BHSPC600_4096chEvent(),)]) == (
        (
            DataLostEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeBHSPC600_4096ch_codegen():
    node = DecodeBHSPC600_4096ch()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_bh_spc600_4096ch<" in code
    assert "DOWN" in code


def test_DecodeBHSPCWithIntensityCounter_accepts_only_BHSPCEvent():
    node = DecodeBHSPCWithIntensityCounter()
    out = node._map_event_sets([(BHSPCEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeBHSPCWithIntensityCounter_output_event_set():
    dt = NumericTraits()
    node = DecodeBHSPCWithIntensityCounter(dt)
    assert node._map_event_sets([(BHSPCEvent(),)]) == (
        (
            BulkCountsEvent(dt),
            DataLostEvent(dt),
            MarkerEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeBHSPCWithIntensityCounter_codegen():
    node = DecodeBHSPCWithIntensityCounter()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_bh_spc_with_intensity_counter<" in code
    assert "DOWN" in code


def test_DecodeBHSPCWithIntensityCounter_compiles_end_to_end():
    from libtcspc._compile import CompiledGraph
    from libtcspc._execute import ExecutionContext
    from libtcspc._graph import Graph
    from libtcspc._processors import SinkAll

    g = Graph()
    g.add_node("dec", DecodeBHSPCWithIntensityCounter())
    g.add_node(None, SinkAll(), upstream="dec")
    cg = CompiledGraph(g, (BHSPCEvent(),))
    ctx = ExecutionContext(cg)
    ctx.flush()


def test_DecodePQT2Generic_accepts_only_matching_input():
    node = DecodePQT2Generic()
    out = node._map_event_sets([(PQT2GenericEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT2Generic_output_event_set():
    dt = NumericTraits()
    node = DecodePQT2Generic(dt)
    assert node._map_event_sets([(PQT2GenericEvent(),)]) == (
        (
            DetectionEvent(dt),
            MarkerEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT2Generic_codegen():
    node = DecodePQT2Generic()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt2_generic<" in code
    assert "DOWN" in code


def test_DecodePQT2HydraHarpV1_accepts_only_matching_input():
    node = DecodePQT2HydraHarpV1()
    out = node._map_event_sets([(PQT2HydraHarpV1Event(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT2HydraHarpV1_output_event_set():
    dt = NumericTraits()
    node = DecodePQT2HydraHarpV1(dt)
    assert node._map_event_sets([(PQT2HydraHarpV1Event(),)]) == (
        (
            DetectionEvent(dt),
            MarkerEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT2HydraHarpV1_codegen():
    node = DecodePQT2HydraHarpV1()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt2_hydraharpv1<" in code
    assert "DOWN" in code


def test_DecodePQT2PicoHarp300_accepts_only_matching_input():
    node = DecodePQT2PicoHarp300()
    out = node._map_event_sets([(PQT2PicoHarp300Event(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT2PicoHarp300_output_event_set():
    dt = NumericTraits()
    node = DecodePQT2PicoHarp300(dt)
    assert node._map_event_sets([(PQT2PicoHarp300Event(),)]) == (
        (
            DetectionEvent(dt),
            MarkerEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT2PicoHarp300_codegen():
    node = DecodePQT2PicoHarp300()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt2_picoharp300<" in code
    assert "DOWN" in code


def test_DecodePQT3Generic_accepts_only_matching_input():
    node = DecodePQT3Generic()
    out = node._map_event_sets([(PQT3GenericEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT3Generic_output_event_set():
    dt = NumericTraits()
    node = DecodePQT3Generic(dt)
    assert node._map_event_sets([(PQT3GenericEvent(),)]) == (
        (
            MarkerEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT3Generic_codegen():
    node = DecodePQT3Generic()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt3_generic<" in code
    assert "DOWN" in code


def test_DecodePQT3HydraHarpV1_accepts_only_matching_input():
    node = DecodePQT3HydraHarpV1()
    out = node._map_event_sets([(PQT3HydraHarpV1Event(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT3HydraHarpV1_output_event_set():
    dt = NumericTraits()
    node = DecodePQT3HydraHarpV1(dt)
    assert node._map_event_sets([(PQT3HydraHarpV1Event(),)]) == (
        (
            MarkerEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT3HydraHarpV1_codegen():
    node = DecodePQT3HydraHarpV1()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt3_hydraharpv1<" in code
    assert "DOWN" in code


def test_DecodePQT3PicoHarp300_accepts_only_matching_input():
    node = DecodePQT3PicoHarp300()
    out = node._map_event_sets([(PQT3PicoHarp300Event(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodePQT3PicoHarp300_output_event_set():
    dt = NumericTraits()
    node = DecodePQT3PicoHarp300(dt)
    assert node._map_event_sets([(PQT3PicoHarp300Event(),)]) == (
        (
            MarkerEvent(dt),
            TimeCorrelatedDetectionEvent(dt),
            TimeReachedEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodePQT3PicoHarp300_codegen():
    node = DecodePQT3PicoHarp300()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_pqt3_picoharp300<" in code
    assert "DOWN" in code


def test_DecodeSwabianTags_accepts_only_matching_input():
    node = DecodeSwabianTags()
    out = node._map_event_sets([(SwabianTagEvent(),)])
    assert len(out) == 1
    with pytest.raises(ValueError):
        node._map_event_sets([(IntEvent,)])


def test_DecodeSwabianTags_output_event_set():
    dt = NumericTraits()
    node = DecodeSwabianTags(dt)
    assert node._map_event_sets([(SwabianTagEvent(),)]) == (
        (
            BeginLostIntervalEvent(dt),
            DetectionEvent(dt),
            EndLostIntervalEvent(dt),
            LostCountsEvent(dt),
            WarningEvent(),
        ),
    )


def test_DecodeSwabianTags_codegen():
    node = DecodeSwabianTags()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::decode_swabian_tags<" in code
    assert "DOWN" in code
