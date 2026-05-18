# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import libtcspc as tcspc
import numpy as np
import pytest
from _test_helpers import _NamedEvent
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName

IntEvent = _NamedEvent(_CppTypeName("int"))

PARAMETERIZED_EVENTS = [
    (tcspc.BeginLostIntervalEvent, "tcspc::begin_lost_interval_event<"),
    (tcspc.BulkCountsEvent, "tcspc::bulk_counts_event<"),
    (tcspc.DataLostEvent, "tcspc::data_lost_event<"),
    (tcspc.DetectionEvent, "tcspc::detection_event<"),
    (tcspc.EndLostIntervalEvent, "tcspc::end_lost_interval_event<"),
    (tcspc.LostCountsEvent, "tcspc::lost_counts_event<"),
    (tcspc.MarkerEvent, "tcspc::marker_event<"),
    (
        tcspc.TimeCorrelatedDetectionEvent,
        "tcspc::time_correlated_detection_event<",
    ),
    (tcspc.TimeReachedEvent, "tcspc::time_reached_event<"),
]

UNTEMPLATED_DEVICE_EVENTS = [
    (tcspc.BHSPC600_256chEvent, "tcspc::bh_spc600_256ch_event"),
    (tcspc.BHSPC600_4096chEvent, "tcspc::bh_spc600_4096ch_event"),
    (tcspc.BHSPCEvent, "tcspc::bh_spc_event"),
    (tcspc.PQT2GenericEvent, "tcspc::pqt2_generic_event"),
    (tcspc.PQT2HydraHarpV1Event, "tcspc::pqt2_hydraharpv1_event"),
    (tcspc.PQT2PicoHarp300Event, "tcspc::pqt2_picoharp300_event"),
    (tcspc.PQT3GenericEvent, "tcspc::pqt3_generic_event"),
    (tcspc.PQT3HydraHarpV1Event, "tcspc::pqt3_hydraharpv1_event"),
    (tcspc.PQT3PicoHarp300Event, "tcspc::pqt3_picoharp300_event"),
    (tcspc.SwabianTagEvent, "tcspc::swabian_tag_event"),
]


@pytest.mark.parametrize(("cls", "cpp_type"), UNTEMPLATED_DEVICE_EVENTS)
def test_untemplated_device_event_cpp_type_name(cls, cpp_type):
    assert cls()._cpp_type_name() == cpp_type


def test_WarningEvent_cpp_type_name():
    assert tcspc.WarningEvent()._cpp_type_name() == "tcspc::warning_event"


@pytest.mark.parametrize(("cls", "prefix"), PARAMETERIZED_EVENTS)
def test_parameterised_event_cpp_type_name_default(cls, prefix):
    name = cls()._cpp_type_name()
    assert name.startswith(prefix)
    assert name.endswith(">")
    assert "tcspc::parameterized_numeric_traits<" in name


@pytest.mark.parametrize(("cls", "prefix"), PARAMETERIZED_EVENTS)
def test_parameterised_event_cpp_type_name_propagates_numeric_traits(
    cls, prefix
):
    name = cls(tcspc.NumericTraits(abstime_type=np.uint64))._cpp_type_name()
    assert "std::uint64_t" in name
    assert "tcspc::default_numeric_traits::abstime_type" not in name


def test_BucketEvent_cpp_type_name_wraps_element():
    assert tcspc.BucketEvent(IntEvent)._cpp_type_name() == "tcspc::bucket<int>"


def test_BucketEvent_cpp_type_name_nests():
    assert (
        tcspc.BucketEvent(tcspc.BHSPCEvent())._cpp_type_name()
        == "tcspc::bucket<tcspc::bh_spc_event>"
    )


def test_BucketEvent_element_event_type_returns_wrapped():
    inner = tcspc.BHSPCEvent()
    be = tcspc.BucketEvent(inner)
    assert be.element_event_type() is inner


def test_BucketEvent_element_event_type_can_be_arbitrary():
    be = tcspc.BucketEvent(IntEvent)
    assert be.element_event_type() is IntEvent


@pytest.mark.parametrize(
    "ev",
    [
        tcspc.BHSPCEvent(),
        tcspc.WarningEvent(),
        tcspc.DataLostEvent(),
        tcspc.BucketEvent(tcspc.BHSPCEvent()),
    ],
)
def test_repr_contains_class_name_and_cpp_type_name(ev):
    r = repr(ev)
    assert r.startswith(f"<{type(ev).__name__}(")
    assert r.endswith(")>")
    assert ev._cpp_type_name() in r


def test_eq_same_singleton_class_is_true():
    assert tcspc.BHSPCEvent() == tcspc.BHSPCEvent()


def test_eq_different_singleton_classes_is_false():
    assert tcspc.BHSPCEvent() != tcspc.WarningEvent()


def test_eq_against_non_EventType_is_false():
    assert tcspc.BHSPCEvent() != object()
    assert tcspc.BHSPCEvent() != "tcspc::bh_spc_event"


@pytest.mark.parametrize(("cls", "prefix"), PARAMETERIZED_EVENTS)
def test_eq_parameterised_same_defaults_is_true(cls, prefix):
    assert cls() == cls()


@pytest.mark.parametrize(("cls", "prefix"), PARAMETERIZED_EVENTS)
def test_eq_parameterised_different_numeric_traits_is_false(cls, prefix):
    assert cls(tcspc.NumericTraits()) != cls(
        tcspc.NumericTraits(abstime_type=np.uint64)
    )


def test_eq_equivalent_but_differently_spelled_numeric_traits_is_true():
    assert tcspc.DataLostEvent(
        tcspc.NumericTraits(abstime_type=np.uint64)
    ) == tcspc.DataLostEvent(tcspc.NumericTraits(abstime_type="uint64"))


def test_eq_BucketEvent_wraps_element_in_equality():
    assert tcspc.BucketEvent(tcspc.BHSPCEvent()) == tcspc.BucketEvent(
        tcspc.BHSPCEvent()
    )
    assert tcspc.BucketEvent(tcspc.BHSPCEvent()) != tcspc.BucketEvent(
        tcspc.WarningEvent()
    )


def test_eq_BucketEvent_vs_inner_is_false():
    assert tcspc.BucketEvent(tcspc.BHSPCEvent()) != tcspc.BHSPCEvent()


def test_default_cpp_input_handler_substrings():
    code = tcspc.WarningEvent()._cpp_input_handler(_CppIdentifier("ds"))
    assert "void handle(tcspc::warning_event const &event)" in code
    assert "ds.handle(event)" in code


def test_default_cpp_output_handlers_substrings():
    code = tcspc.WarningEvent()._cpp_output_handlers(_CppIdentifier("sink"))
    assert "void handle(tcspc::warning_event const &event)" in code
    assert "void handle(tcspc::warning_event &&event)" in code
    assert "nanobind::gil_scoped_acquire" in code
    assert code.count("sink->handle(") == 2
    assert "nanobind::rv_policy::copy" in code
    assert "nanobind::rv_policy::move" in code


def test_BucketEvent_cpp_input_handler_uses_ad_hoc_bucket():
    code = tcspc.BucketEvent(IntEvent)._cpp_input_handler(_CppIdentifier("ds"))
    assert (
        "nanobind::ndarray<int const, nanobind::device::cpu, nanobind::c_contig>"
        in code
    )
    assert "tcspc::ad_hoc_bucket" in code
    assert "ds.handle(bkt)" in code


def test_BucketEvent_cpp_output_handlers_cover_three_overloads():
    code = tcspc.BucketEvent(IntEvent)._cpp_output_handlers(
        _CppIdentifier("sink")
    )
    assert "handle(tcspc::bucket<int const> const &event)" in code
    assert "handle(tcspc::bucket<int> const &event)" in code
    assert "handle(tcspc::bucket<int> &&event)" in code
    assert "nanobind::ndarray<elem_type, nanobind::numpy>" in code
    assert "emit_span_copy(" in code
    assert "nanobind::capsule" in code
    assert "nanobind::gil_scoped_acquire" in code
