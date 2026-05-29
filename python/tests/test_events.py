# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import re

import libtcspc as tcspc
import numpy as np
import pytest
from _test_helpers import _NamedEvent
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName

IntEvent = _NamedEvent(_CppTypeName("int"))

PARAMETERIZED_EVENTS = [
    (tcspc.BeginLostIntervalEvent, "tcspc::begin_lost_interval_event<"),
    (tcspc.BinIncrementClusterEvent, "tcspc::bin_increment_cluster_event<"),
    (tcspc.BinIncrementEvent, "tcspc::bin_increment_event<"),
    (tcspc.BulkCountsEvent, "tcspc::bulk_counts_event<"),
    (
        tcspc.ConcludingHistogramArrayEvent,
        "tcspc::concluding_histogram_array_event<",
    ),
    (tcspc.ConcludingHistogramEvent, "tcspc::concluding_histogram_event<"),
    (tcspc.DataLostEvent, "tcspc::data_lost_event<"),
    (tcspc.DatapointEvent, "tcspc::datapoint_event<"),
    (tcspc.DetectionEvent, "tcspc::detection_event<"),
    (tcspc.EndLostIntervalEvent, "tcspc::end_lost_interval_event<"),
    (tcspc.HistogramArrayEvent, "tcspc::histogram_array_event<"),
    (
        tcspc.HistogramArrayProgressEvent,
        "tcspc::histogram_array_progress_event<",
    ),
    (tcspc.HistogramEvent, "tcspc::histogram_event<"),
    (tcspc.LostCountsEvent, "tcspc::lost_counts_event<"),
    (tcspc.MarkerEvent, "tcspc::marker_event<"),
    (
        tcspc.PeriodicSequenceModelEvent,
        "tcspc::periodic_sequence_model_event<",
    ),
    (tcspc.RealLinearTimingEvent, "tcspc::real_linear_timing_event<"),
    (tcspc.RealOneShotTimingEvent, "tcspc::real_one_shot_timing_event<"),
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
    assert name.endswith("<tcspc::default_numeric_traits>")


@pytest.mark.parametrize(("cls", "prefix"), PARAMETERIZED_EVENTS)
def test_parameterised_event_cpp_type_name_propagates_numeric_traits(
    cls, prefix
):
    name = cls(tcspc.NumericTraits(abstime_type=np.uint64))._cpp_type_name()
    assert name.startswith(prefix)
    inner = name[len(prefix) : -1]
    assert inner.startswith("nt_")


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


def test_default_cpp_output_handlers_raise():
    with pytest.raises(TypeError, match="tcspc::warning_event"):
        tcspc.WarningEvent()._cpp_output_handlers(_CppIdentifier("sink"))


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


def test_ConstBucketEvent_cpp_type_name_wraps_element_as_const():
    assert (
        tcspc.ConstBucketEvent(IntEvent)._cpp_type_name()
        == "tcspc::bucket<int const>"
    )


def test_ConstBucketEvent_cpp_type_name_nests():
    assert (
        tcspc.ConstBucketEvent(tcspc.BHSPCEvent())._cpp_type_name()
        == "tcspc::bucket<tcspc::bh_spc_event const>"
    )


def test_ConstBucketEvent_element_event_type_returns_wrapped():
    inner = tcspc.BHSPCEvent()
    be = tcspc.ConstBucketEvent(inner)
    assert be.element_event_type() is inner


def test_ConstBucketEvent_is_distinct_from_BucketEvent():
    assert tcspc.ConstBucketEvent(IntEvent) != tcspc.BucketEvent(IntEvent)
    assert tcspc.ConstBucketEvent(IntEvent) == tcspc.ConstBucketEvent(IntEvent)


def test_ConstBucketEvent_cpp_input_handler_uses_ad_hoc_bucket():
    code = tcspc.ConstBucketEvent(IntEvent)._cpp_input_handler(
        _CppIdentifier("ds")
    )
    assert (
        "nanobind::ndarray<int const, nanobind::device::cpu, nanobind::c_contig>"
        in code
    )
    assert "tcspc::ad_hoc_bucket" in code
    assert "ds.handle(" in code


def test_ConstBucketEvent_cpp_output_handlers_are_read_only():
    code = tcspc.ConstBucketEvent(IntEvent)._cpp_output_handlers(
        _CppIdentifier("sink")
    )
    assert "handle(tcspc::bucket<int const> const &event)" in code
    assert "nanobind::ndarray<elem_type const, nanobind::numpy>" in code
    assert "emit_span_copy(" in code
    # Read-only delivery only: no writable ndarray and no rvalue overload.
    assert "nanobind::ndarray<elem_type, nanobind::numpy>" not in code
    assert "handle(tcspc::bucket<int> const &event)" not in code
    assert "&&event" not in code


def test_VariantEvent_cpp_type_name():
    ev = tcspc.VariantEvent(tcspc.BHSPCEvent(), tcspc.WarningEvent())
    assert ev._cpp_type_name() == (
        "tcspc::variant_event<tcspc::type_list<"
        "tcspc::bh_spc_event, tcspc::warning_event>>"
    )


def test_VariantEvent_single_member_cpp_type_name():
    assert (
        tcspc.VariantEvent(tcspc.BHSPCEvent())._cpp_type_name()
        == "tcspc::variant_event<tcspc::type_list<tcspc::bh_spc_event>>"
    )


def test_VariantEvent_requires_at_least_one_member():
    with pytest.raises(ValueError, match="at least one"):
        tcspc.VariantEvent()


def test_VariantEvent_eq_same_members_same_order():
    assert tcspc.VariantEvent(
        tcspc.BHSPCEvent(), tcspc.WarningEvent()
    ) == tcspc.VariantEvent(tcspc.BHSPCEvent(), tcspc.WarningEvent())


def test_VariantEvent_eq_order_is_significant():
    assert tcspc.VariantEvent(
        tcspc.BHSPCEvent(), tcspc.WarningEvent()
    ) != tcspc.VariantEvent(tcspc.WarningEvent(), tcspc.BHSPCEvent())


def test_VariantEvent_does_not_support_value():
    with pytest.raises(TypeError, match="does not support"):
        tcspc.VariantEvent(tcspc.BHSPCEvent()).value()


def test_VariantEvent_not_deliverable_to_python_sink():
    with pytest.raises(TypeError, match="not supported for delivery"):
        tcspc.VariantEvent(tcspc.BHSPCEvent())._cpp_output_handlers(
            _CppIdentifier("sink")
        )


def test_DetectionPairEvent_cpp_type_name():
    name = tcspc.DetectionPairEvent()._cpp_type_name()
    assert name == (
        "std::array<tcspc::detection_event<tcspc::default_numeric_traits>, 2>"
    )


EXTRACTABLE_EVENTS = [
    tcspc.HistogramEvent,
    tcspc.ConcludingHistogramEvent,
    tcspc.HistogramArrayEvent,
    tcspc.HistogramArrayProgressEvent,
    tcspc.ConcludingHistogramArrayEvent,
]


@pytest.mark.parametrize("cls", EXTRACTABLE_EVENTS)
def test_extractable_event_element_is_bin_type(cls):
    elem = cls()._data_bucket_element_event_type()
    assert elem._cpp_type_name() == "tcspc::default_numeric_traits::bin_type"


def test_bin_increment_cluster_event_is_not_extractable():
    assert not hasattr(
        tcspc.BinIncrementClusterEvent(), "_data_bucket_element_event_type"
    )


FIELDS_EXPECTED = [
    (tcspc.BeginLostIntervalEvent, ["abstime"]),
    (tcspc.DataLostEvent, ["abstime"]),
    (tcspc.DetectionEvent, ["abstime", "channel"]),
    (tcspc.EndLostIntervalEvent, ["abstime"]),
    (tcspc.MarkerEvent, ["abstime", "channel"]),
    (tcspc.TimeCorrelatedDetectionEvent, ["abstime", "channel", "difftime"]),
    (tcspc.TimeReachedEvent, ["abstime"]),
]


@pytest.mark.parametrize(("cls", "names"), FIELDS_EXPECTED)
def test_fields_returns_expected_schema(cls, names):
    schema = cls()._fields()
    assert [n for n, _ in schema] == names
    prefix = "tcspc::default_numeric_traits::"
    for name, ctype in schema:
        assert ctype == f"{prefix}{name}_type"


def test_fields_unsupported_type_raises():
    with pytest.raises(TypeError, match="does not support"):
        tcspc.BucketEvent(IntEvent)._fields()
    with pytest.raises(TypeError, match="does not support"):
        tcspc.WarningEvent()._fields()


def test_value_happy_path():
    inst = tcspc.DetectionEvent().value(abstime=42, channel=1)
    assert isinstance(inst, tcspc.EventInstance)
    assert inst._fields == {"abstime": 42, "channel": 1}


def test_value_orders_fields_by_schema_not_kwargs():
    inst = tcspc.DetectionEvent().value(channel=1, abstime=42)
    assert list(inst._fields) == ["abstime", "channel"]


def test_value_missing_field_raises():
    with pytest.raises(TypeError, match="missing"):
        tcspc.DetectionEvent().value(abstime=42)


def test_value_unexpected_field_raises():
    with pytest.raises(TypeError, match="unexpected"):
        tcspc.DetectionEvent().value(abstime=42, channel=1, bogus=0)


def test_value_non_int_value_raises():
    with pytest.raises(TypeError, match="must be an int"):
        tcspc.DetectionEvent().value(abstime=42, channel=1.5)  # type: ignore[arg-type]


def test_value_bool_value_raises():
    with pytest.raises(TypeError, match="must be an int"):
        tcspc.DetectionEvent().value(abstime=42, channel=True)


def test_value_unsupported_type_raises():
    with pytest.raises(TypeError, match="does not support"):
        tcspc.WarningEvent().value()


def test_cpp_expression_default_traits():
    inst = tcspc.DetectionEvent().value(abstime=42, channel=1)
    assert inst._cpp_expression() == (
        "tcspc::detection_event<tcspc::default_numeric_traits>{"
        ".abstime = static_cast<tcspc::default_numeric_traits::abstime_type>(42), "
        ".channel = static_cast<tcspc::default_numeric_traits::channel_type>(1)}"
    )


def test_cpp_expression_substitutes_numeric_traits_member_types():
    nt = tcspc.NumericTraits(abstime_type=np.uint64)
    inst = tcspc.DetectionEvent(nt).value(abstime=42, channel=1)
    expr = inst._cpp_expression()
    nt_name = tcspc.DetectionEvent(nt)._cpp_type_name()
    assert expr.startswith(f"{nt_name}{{")
    assert ".abstime = static_cast<" in expr
    assert "::abstime_type>(42)" in expr
    assert ".channel = static_cast<" in expr
    assert "::channel_type>(1)" in expr


def test_event_instance_eq_same_is_equal():
    a = tcspc.DetectionEvent().value(abstime=42, channel=1)
    b = tcspc.DetectionEvent().value(abstime=42, channel=1)
    assert a == b


def test_event_instance_eq_different_fields_differ():
    a = tcspc.DetectionEvent().value(abstime=42, channel=1)
    b = tcspc.DetectionEvent().value(abstime=42, channel=2)
    assert a != b


def test_event_instance_eq_different_types_differ():
    a = tcspc.DetectionEvent().value(abstime=42, channel=1)
    b = tcspc.MarkerEvent().value(abstime=42, channel=1)
    assert a != b


def test_event_instance_eq_against_non_instance_is_false():
    assert tcspc.DetectionEvent().value(abstime=42, channel=1) != object()


def test_event_instance_repr():
    inst = tcspc.DetectionEvent().value(abstime=42, channel=1)
    assert repr(inst) == "DetectionEvent(...).value(abstime=42, channel=1)"


def test_supports_value():
    assert tcspc.DetectionEvent()._supports_value()
    assert not tcspc.WarningEvent()._supports_value()
    assert not tcspc.BucketEvent(IntEvent)._supports_value()


def test_value_supporting_cpp_output_handlers_emit_wrapper_cast():
    code = tcspc.DetectionEvent()._cpp_output_handlers(_CppIdentifier("sink"))
    assert (
        "handle(tcspc::detection_event<tcspc::default_numeric_traits> "
        "const &event)" in code
    )
    assert "nanobind::gil_scoped_acquire" in code
    assert "sink->handle(nanobind::cast(event))" in code


def test_cpp_wrapper_class_def_substrings():
    code = tcspc.DetectionEvent()._cpp_wrapper_class_def(_CppIdentifier("mod"))
    assert (
        "nanobind::class_<tcspc::detection_event"
        "<tcspc::default_numeric_traits>>" in code
    )
    assert ".def(nanobind::init<>())" in code
    assert '.def_rw("abstime"' in code
    assert '.def_rw("channel"' in code


def test_CustomEvent_empty_cpp_type_name():
    name = tcspc.CustomEvent("ce_empty_name")._cpp_type_name()
    assert re.fullmatch(r"ce_ce_empty_name_[0-9a-f]{16}", name)


def test_CustomEvent_abstime_cpp_type_name():
    ev = tcspc.CustomEvent(
        "ce_abstime_name", abstime=True, traits=tcspc.NumericTraits()
    )
    assert re.fullmatch(
        r"ce_ce_abstime_name_[0-9a-f]{16}", ev._cpp_type_name()
    )


def test_CustomEvent_empty_fields_is_empty():
    assert tcspc.CustomEvent("ce_empty_fields")._fields() == []


def test_CustomEvent_abstime_fields():
    ev = tcspc.CustomEvent(
        "ce_abstime_fields", abstime=True, traits=tcspc.NumericTraits()
    )
    assert ev._fields() == [
        ("abstime", "tcspc::default_numeric_traits::abstime_type")
    ]


def test_CustomEvent_abstime_fields_use_supplied_traits():
    nt = tcspc.NumericTraits(abstime_type=np.uint64)
    ev = tcspc.CustomEvent("ce_abstime_traits", abstime=True, traits=nt)
    ((_, ctype),) = ev._fields()
    assert ctype == f"{nt._cpp_type_name()}::abstime_type"


def test_CustomEvent_traits_required_when_abstime():
    with pytest.raises(TypeError, match="traits is required"):
        tcspc.CustomEvent("ce_needs_traits", abstime=True)


def test_CustomEvent_traits_forbidden_when_not_abstime():
    with pytest.raises(TypeError, match="must not be given"):
        tcspc.CustomEvent("ce_extra_traits", traits=tcspc.NumericTraits())


@pytest.mark.parametrize(
    "name", ["", "1bad", "has space", "has-dash", "ns::name", "a.b"]
)
def test_CustomEvent_invalid_identifier_raises(name):
    with pytest.raises(ValueError, match="not a valid C\\+\\+ identifier"):
        tcspc.CustomEvent(name)


def test_CustomEvent_same_name_same_shape_equal():
    a = tcspc.CustomEvent("ce_same_shape")
    b = tcspc.CustomEvent("ce_same_shape")
    assert a == b
    assert a._cpp_type_name() == b._cpp_type_name()


def test_CustomEvent_distinct_names_not_equal():
    assert tcspc.CustomEvent("ce_distinct_a") != tcspc.CustomEvent(
        "ce_distinct_b"
    )


def test_CustomEvent_same_name_different_shape_distinct():
    empty = tcspc.CustomEvent("ce_same_name_diff_shape")
    abst = tcspc.CustomEvent(
        "ce_same_name_diff_shape", abstime=True, traits=tcspc.NumericTraits()
    )
    assert empty._cpp_type_name() != abst._cpp_type_name()
    assert empty != abst


def test_CustomEvent_cpp_type_name_is_deterministic():
    a = tcspc.CustomEvent(
        "ce_determ", abstime=True, traits=tcspc.NumericTraits()
    )
    b = tcspc.CustomEvent(
        "ce_determ", abstime=True, traits=tcspc.NumericTraits()
    )
    assert a._cpp_type_name() == b._cpp_type_name()


def test_CustomEvent_equivalent_traits_collapse():
    a = tcspc.CustomEvent(
        "ce_equiv_traits",
        abstime=True,
        traits=tcspc.NumericTraits(abstime_type=np.uint64),
    )
    b = tcspc.CustomEvent(
        "ce_equiv_traits",
        abstime=True,
        traits=tcspc.NumericTraits(abstime_type="uint64"),
    )
    assert a._cpp_type_name() == b._cpp_type_name()
    assert a == b


def test_CustomEvent_repeated_identical_definition_ok():
    nt = tcspc.NumericTraits()
    tcspc.CustomEvent("ce_repeat", abstime=True, traits=nt)
    tcspc.CustomEvent("ce_repeat", abstime=True, traits=nt)


def test_CustomEvent_same_name_different_traits_distinct():
    # Different custom traits give distinct content-addressed struct names, so
    # the events are distinct types.
    a = tcspc.CustomEvent(
        "ce_diff_traits",
        abstime=True,
        traits=tcspc.NumericTraits(abstime_type=np.uint64),
    )
    b = tcspc.CustomEvent(
        "ce_diff_traits",
        abstime=True,
        traits=tcspc.NumericTraits(abstime_type=np.uint32),
    )
    assert a._cpp_type_name() != b._cpp_type_name()
    assert a != b


def test_CustomEvent_supports_value():
    assert tcspc.CustomEvent("ce_supports_empty")._supports_value()
    assert tcspc.CustomEvent(
        "ce_supports_abstime", abstime=True, traits=tcspc.NumericTraits()
    )._supports_value()


def test_CustomEvent_empty_value_cpp_expression():
    ev = tcspc.CustomEvent("ce_value_empty")
    assert ev.value()._cpp_expression() == f"{ev._cpp_type_name()}{{}}"


def test_CustomEvent_abstime_value_cpp_expression():
    ev = tcspc.CustomEvent(
        "ce_value_abstime", abstime=True, traits=tcspc.NumericTraits()
    )
    expr = ev.value(abstime=7)._cpp_expression()
    assert expr == (
        f"{ev._cpp_type_name()}{{.abstime = "
        "static_cast<tcspc::default_numeric_traits::abstime_type>(7)}"
    )


def test_CustomEvent_empty_definition_substrings():
    ev = tcspc.CustomEvent("ce_def_empty")
    cpp = ev._cpp_type_name()
    assert f"struct {cpp} {{" in ev._definition
    assert (
        f"operator==({cpp} const &, {cpp} const &) -> bool = default;"
        in ev._definition
    )
    # operator<< prints the bare readable name, not the hashed type name.
    assert 's << "ce_def_empty"' in ev._definition


def test_CustomEvent_abstime_definition_substrings():
    ev = tcspc.CustomEvent(
        "ce_def_abstime", abstime=True, traits=tcspc.NumericTraits()
    )
    cpp = ev._cpp_type_name()
    assert f"struct {cpp} {{" in ev._definition
    assert "tcspc::default_numeric_traits::abstime_type abstime;" in (
        ev._definition
    )
    assert 's << "ce_def_abstime{abstime=" << e.abstime << "}"' in (
        ev._definition
    )


def test_CustomEvent_repr():
    assert repr(tcspc.CustomEvent("ce_repr")) == "<CustomEvent(ce_repr)>"
