# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

from collections.abc import Callable, Mapping, Sequence
from typing import Any, cast

from libtcspc._compile import CompiledGraph
from libtcspc._execute import _build_execution
from libtcspc._param import Param


class _FakeParams:
    pass


class _FakeModulePySink:
    def __init__(self) -> None:
        pass


class _FakeMod:
    Params = _FakeParams
    PySink = _FakeModulePySink

    def create_context(self) -> object:
        return object()

    def create_processor(
        self, ctx: object, params: object, sinks: object
    ) -> object:
        # Return the populated Params struct so the test can inspect it.
        return params


class _FakeCompiledGraph:
    """Minimal stand-in for `CompiledGraph` exercising the encoder layer in
    `_build_execution` without invoking the C++ toolchain."""

    def __init__(
        self,
        params: Sequence[Param],
        encoders: Mapping[str, Callable[[Any], Any]],
    ) -> None:
        self._mod = _FakeMod()
        self._params = tuple(params)
        self._param_encoders = encoders
        self._eventtype_by_wrapper: dict[type, Any] = {}
        self._wrapper_by_cpp: dict[str, Any] = {}

    def parameters(self) -> tuple[Param, ...]:
        return self._params

    def _accesses(self) -> tuple[Any, ...]:
        return ()


def test_encoder_applied_to_given_argument():
    p: Param = Param("ci")
    cg = _FakeCompiledGraph(
        (p,),
        {"ci": lambda m: [(int(c), int(i)) for c, i in dict(m).items()]},
    )
    _, _, params, _, _ = _build_execution(
        cast(CompiledGraph, cg), {"ci": {0: 1, 5: 0}}, None
    )
    assert getattr(params, p._cpp_identifier()) == [(0, 1), (5, 0)]


def test_encoder_applied_to_default_value():
    p: Param = Param("ci", {0: 1})
    cg = _FakeCompiledGraph(
        (p,),
        {"ci": lambda m: [(int(c), int(i)) for c, i in dict(m).items()]},
    )
    _, _, params, _, _ = _build_execution(cast(CompiledGraph, cg), None, None)
    assert getattr(params, p._cpp_identifier()) == [(0, 1)]


def test_value_passed_through_when_no_encoder():
    p: Param = Param("n")
    cg = _FakeCompiledGraph((p,), {})
    _, _, params, _, _ = _build_execution(
        cast(CompiledGraph, cg), {"n": 42}, None
    )
    assert getattr(params, p._cpp_identifier()) == 42
