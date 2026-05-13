# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import dataclasses
from collections.abc import Sequence

import pytest
from _test_helpers import _TestRelayNode
from libtcspc._cpp_utils import _CppTypeName
from libtcspc._graph import Graph
from libtcspc._param import Param, _Parameterized


def test_param_required():
    p: Param = Param("x")
    assert p.name == "x"
    assert p.default_value is None


def test_param_with_default_value():
    p = Param("x", 7)
    assert p.name == "x"
    assert p.default_value == 7


def test_param_explicit_none_default():
    assert Param("x", None) == Param("x")


def test_param_is_frozen():
    p = Param("x", 1)
    with pytest.raises(dataclasses.FrozenInstanceError):
        p.name = "y"  # type: ignore[misc]
    with pytest.raises(dataclasses.FrozenInstanceError):
        p.default_value = 2  # type: ignore[misc]


def test_param_equality():
    assert Param("x") == Param("x")
    assert Param("x", 1) == Param("x", 1)
    assert Param("x") != Param("y")
    assert Param("x", 1) != Param("x", 2)
    assert Param("x") != Param("x", 1)


def test_param_hashable():
    assert hash(Param("x")) == hash(Param("x"))
    s: set[Param] = {Param("x"), Param("x"), Param("y")}
    assert s == {Param("x"), Param("y")}


def test_param_repr():
    r = repr(Param("x", 7))
    assert "x" in r
    assert "7" in r


def test_param_generic_runtime_behaviour():
    # Generics are erased at runtime; Param[int]("n", 3) and Param("n", 3) are
    # equivalent objects.
    a: Param = Param[int]("n", 3)
    b = Param("n", 3)
    assert a == b
    assert a.name == "n"
    assert a.default_value == 3


def test_param_cpp_identifier_simple():
    assert Param("abc")._cpp_identifier() == "z_abc"


def test_param_cpp_identifier_special_chars():
    # Pin the contract that Param uses identifier mangling (a duplicate of
    # test_cpp_utils_identifier_from_string assertions on purpose).
    assert Param("a-b")._cpp_identifier() == "z_aQ2db"
    assert Param("_x")._cpp_identifier() == "z_Q5fx"
    assert Param("")._cpp_identifier() == "z_"


def test_param_cpp_identifier_collision_via_name():
    assert Param("abc")._cpp_identifier() == Param("abc")._cpp_identifier()
    assert Param("abc")._cpp_identifier() != Param("abd")._cpp_identifier()


def test_parameterized_default_parameters_empty():
    class _Plain(_Parameterized):
        pass

    assert _Plain()._parameters() == ()


def test_parameterized_can_be_subclassed_with_params():
    class _ParamNode(_TestRelayNode):
        def _parameters(self) -> Sequence[tuple[Param, _CppTypeName]]:
            return ((Param("hello", 42), _CppTypeName("int")),)

    g = Graph()
    g.add_node("n", _ParamNode())
    params = g._parameters()
    assert len(params) == 1
    assert params[0] == (Param("hello", 42), _CppTypeName("int"))
