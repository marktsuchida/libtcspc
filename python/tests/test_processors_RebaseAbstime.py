# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from _test_helpers import _NamedEvent
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import _CppExpression, _CppIdentifier, _CppTypeName
from libtcspc._numeric_traits import NumericTraits
from libtcspc._processors import RebaseAbstime

IntEvent = _NamedEvent(_CppTypeName("int"))
OtherEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_RebaseAbstime_event_set_is_preserved():
    node = RebaseAbstime()
    assert node._map_event_sets([(IntEvent, OtherEvent)]) == (
        (IntEvent, OtherEvent),
    )


def test_RebaseAbstime_codegen():
    node = RebaseAbstime()
    code = node._cpp_expression(gencontext, [_CppExpression("DOWN")])
    assert "tcspc::rebase_abstime<" in code
    assert NumericTraits()._cpp_type_name() in code
    assert "DOWN" in code
