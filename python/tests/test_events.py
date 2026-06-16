# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import re

import libtcspc as tcspc
import numpy as np
import pytest
from _test_helpers import _NamedEvent
from libtcspc._cpp_utils import _CppIdentifier, _CppTypeName
from libtcspc._events import _ArrayField, _BucketField, _ScalarField

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
    with pytest.raises(TypeError, match="tcspc::bh_spc_event"):
        tcspc.BHSPCEvent()._cpp_output_handlers(_CppIdentifier("sink"))


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
    with pytest.raises(
        TypeError, match="cannot be delivered to a Python sink"
    ):
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
    (tcspc.BinIncrementEvent, ["bin_index"]),
    (tcspc.BulkCountsEvent, ["abstime", "channel", "count"]),
    (tcspc.DataLostEvent, ["abstime"]),
    (tcspc.DetectionEvent, ["abstime", "channel"]),
    (tcspc.EndLostIntervalEvent, ["abstime"]),
    (tcspc.LostCountsEvent, ["abstime", "channel", "count"]),
    (tcspc.MarkerEvent, ["abstime", "channel"]),
    (tcspc.TimeCorrelatedDetectionEvent, ["abstime", "channel", "difftime"]),
    (tcspc.TimeReachedEvent, ["abstime"]),
]


@pytest.mark.parametrize(("cls", "names"), FIELDS_EXPECTED)
def test_fields_returns_expected_schema(cls, names):
    schema = cls()._field_schema()
    assert [f.name for f in schema] == names
    prefix = "tcspc::default_numeric_traits::"
    for f in schema:
        assert isinstance(f, _ScalarField)
        assert f.cpp_type == f"{prefix}{f.name}_type"


FIELDS_EXPECTED_EXPLICIT = [
    (
        tcspc.DatapointEvent,
        [
            _ScalarField(
                "value",
                _CppTypeName("tcspc::default_numeric_traits::datapoint_type"),
            )
        ],
    ),
    (
        tcspc.PeriodicSequenceModelEvent,
        [
            _ScalarField(
                "abstime",
                _CppTypeName("tcspc::default_numeric_traits::abstime_type"),
            ),
            _ScalarField("delay", _CppTypeName("double")),
            _ScalarField("interval", _CppTypeName("double")),
        ],
    ),
    (
        tcspc.RealLinearTimingEvent,
        [
            _ScalarField(
                "abstime",
                _CppTypeName("tcspc::default_numeric_traits::abstime_type"),
            ),
            _ScalarField("delay", _CppTypeName("double")),
            _ScalarField("interval", _CppTypeName("double")),
            _ScalarField("count", _CppTypeName("std::size_t")),
        ],
    ),
    (
        tcspc.RealOneShotTimingEvent,
        [
            _ScalarField(
                "abstime",
                _CppTypeName("tcspc::default_numeric_traits::abstime_type"),
            ),
            _ScalarField("delay", _CppTypeName("double")),
        ],
    ),
    (
        tcspc.WarningEvent,
        [_ScalarField("message", _CppTypeName("std::string"))],
    ),
]


@pytest.mark.parametrize(("cls", "schema"), FIELDS_EXPECTED_EXPLICIT)
def test_fields_returns_expected_explicit_schema(cls, schema):
    assert cls()._field_schema() == schema


def test_field_schema_unsupported_type_is_none():
    assert tcspc.BucketEvent(IntEvent)._field_schema() is None


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


def test_value_float_field_happy_path():
    inst = tcspc.RealOneShotTimingEvent().value(abstime=1, delay=2.5)
    assert inst._fields == {"abstime": 1, "delay": 2.5}
    assert "static_cast<double>(2.5)" in inst._cpp_expression()


def test_value_float_field_accepts_int():
    inst = tcspc.RealOneShotTimingEvent().value(abstime=1, delay=2)
    assert inst._fields == {"abstime": 1, "delay": 2.0}
    assert "static_cast<double>(2.0)" in inst._cpp_expression()


def test_value_float_field_rejects_str():
    with pytest.raises(TypeError, match="must be a float"):
        tcspc.RealOneShotTimingEvent().value(abstime=1, delay="x")  # type: ignore[arg-type]


def test_value_float_field_rejects_bool():
    with pytest.raises(TypeError, match="must be a float"):
        tcspc.RealOneShotTimingEvent().value(abstime=1, delay=True)


def test_value_str_field_happy_path():
    inst = tcspc.WarningEvent().value(message="hi")
    assert inst._fields == {"message": "hi"}
    assert 'std::string{"hi"}' in inst._cpp_expression()


def test_value_str_field_escapes_special_chars():
    inst = tcspc.WarningEvent().value(message='a"b\\c\nd')
    expr = inst._cpp_expression()
    assert 'std::string{"a\\"b\\\\c\\nd"}' in expr


def test_value_str_field_rejects_int():
    with pytest.raises(TypeError, match="must be a str"):
        tcspc.WarningEvent().value(message=1)  # type: ignore[arg-type]


def test_value_unsupported_type_raises():
    with pytest.raises(TypeError, match="does not support"):
        tcspc.BucketEvent(IntEvent).value()


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


def test_event_instance_getattr_int_fields():
    inst = tcspc.DetectionEvent().value(abstime=42, channel=1)
    assert inst.abstime == 42
    assert inst.channel == 1


def test_event_instance_getattr_float_field():
    inst = tcspc.RealOneShotTimingEvent().value(abstime=1, delay=2.5)
    assert inst.abstime == 1
    assert inst.delay == 2.5


def test_event_instance_getattr_str_field():
    inst = tcspc.WarningEvent().value(message="hi")
    assert inst.message == "hi"


def test_event_instance_getattr_unknown_field_raises():
    inst = tcspc.DetectionEvent().value(abstime=42, channel=1)
    with pytest.raises(AttributeError):
        _ = inst.nonesuch


def test_event_instance_dir_includes_fields():
    inst = tcspc.DetectionEvent().value(abstime=42, channel=1)
    names = dir(inst)
    assert "abstime" in names
    assert "channel" in names


def test_supports_value():
    assert tcspc.DetectionEvent()._supports_value()
    assert tcspc.WarningEvent()._supports_value()
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
    assert tcspc.CustomEvent("ce_empty_fields")._field_schema() == []


def test_CustomEvent_abstime_fields():
    ev = tcspc.CustomEvent(
        "ce_abstime_fields", abstime=True, traits=tcspc.NumericTraits()
    )
    assert ev._field_schema() == [
        _ScalarField(
            "abstime",
            _CppTypeName("tcspc::default_numeric_traits::abstime_type"),
        )
    ]


def test_CustomEvent_abstime_fields_use_supplied_traits():
    nt = tcspc.NumericTraits(abstime_type=np.uint64)
    ev = tcspc.CustomEvent("ce_abstime_traits", abstime=True, traits=nt)
    (f,) = ev._field_schema()
    assert isinstance(f, _ScalarField)
    assert f.cpp_type == f"{nt._cpp_type_name()}::abstime_type"


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


BUCKET_FIELD_EVENTS = [
    (tcspc.HistogramEvent, "data_bucket", "bin_type"),
    (tcspc.ConcludingHistogramEvent, "data_bucket", "bin_type"),
    (tcspc.HistogramArrayEvent, "data_bucket", "bin_type"),
    (tcspc.ConcludingHistogramArrayEvent, "data_bucket", "bin_type"),
    (tcspc.HistogramArrayProgressEvent, "data_bucket", "bin_type"),
    (tcspc.BinIncrementClusterEvent, "bin_indices", "bin_index_type"),
]


@pytest.mark.parametrize(("cls", "fname", "member"), BUCKET_FIELD_EVENTS)
def test_bucket_field_schema_default_traits(cls, fname, member):
    f = cls()._field_schema()[-1]
    assert isinstance(f, _BucketField)
    assert f.name == fname
    assert f.element_cpp_type == f"tcspc::default_numeric_traits::{member}"
    assert f.dtype == np.dtype(np.uint16)


def test_progress_event_schema_has_valid_size_first():
    schema = tcspc.HistogramArrayProgressEvent()._field_schema()
    assert [f.name for f in schema] == ["valid_size", "data_bucket"]
    assert schema[0] == _ScalarField("valid_size", _CppTypeName("std::size_t"))


def test_bucket_field_schema_propagates_numeric_traits():
    nt = tcspc.NumericTraits(bin_type=np.uint32)
    (f,) = tcspc.HistogramEvent(nt)._field_schema()
    assert isinstance(f, _BucketField)
    assert f.element_cpp_type == f"{nt._cpp_type_name()}::bin_type"
    assert f.dtype == np.dtype(np.uint32)


def test_bin_increment_cluster_schema_propagates_numeric_traits():
    nt = tcspc.NumericTraits(bin_index_type=np.uint32)
    (f,) = tcspc.BinIncrementClusterEvent(nt)._field_schema()
    assert isinstance(f, _BucketField)
    assert f.element_cpp_type == f"{nt._cpp_type_name()}::bin_index_type"
    assert f.dtype == np.dtype(np.uint32)


@pytest.mark.parametrize(("cls", "fname", "member"), BUCKET_FIELD_EVENTS)
def test_bucket_field_event_supports_value(cls, fname, member):
    assert cls()._supports_value()
    assert cls()._has_bucket_fields()


def test_has_bucket_fields_false_for_scalar_and_unsupported_events():
    assert not tcspc.DetectionEvent()._has_bucket_fields()
    assert not tcspc.BucketEvent(IntEvent)._has_bucket_fields()


def test_value_bucket_field_from_list():
    inst = tcspc.BinIncrementClusterEvent().value(bin_indices=[1, 2, 3])
    arr = inst.bin_indices
    assert isinstance(arr, np.ndarray)
    assert arr.dtype == np.dtype(np.uint16)
    assert arr.flags.writeable is False
    assert list(arr) == [1, 2, 3]


def test_value_bucket_field_from_exact_dtype_ndarray_stores_copy():
    src = np.array([1, 2, 3], dtype=np.uint16)
    inst = tcspc.BinIncrementClusterEvent().value(bin_indices=src)
    src[0] = 99
    arr = inst.bin_indices
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [1, 2, 3]
    assert arr.flags.writeable is False


def test_value_bucket_field_safe_cast_ndarray():
    src = np.array([1, 2, 3], dtype=np.uint8)
    inst = tcspc.BinIncrementClusterEvent().value(bin_indices=src)
    arr = inst.bin_indices
    assert isinstance(arr, np.ndarray)
    assert arr.dtype == np.dtype(np.uint16)


def test_value_bucket_field_unsafe_cast_ndarray_raises():
    src = np.array([1, 2, 3], dtype=np.int32)
    with pytest.raises(TypeError, match="safely cast"):
        tcspc.BinIncrementClusterEvent().value(bin_indices=src)


def test_value_bucket_field_out_of_range_list_raises():
    with pytest.raises(TypeError, match="bin_indices"):
        tcspc.BinIncrementClusterEvent().value(bin_indices=[70000])


def test_value_bucket_field_rejects_2d():
    with pytest.raises(TypeError, match="1-dimensional"):
        tcspc.BinIncrementClusterEvent().value(
            bin_indices=np.zeros((2, 2), dtype=np.uint16)
        )


def test_value_bucket_field_rejects_scalar():
    with pytest.raises(TypeError, match="1-dimensional"):
        tcspc.BinIncrementClusterEvent().value(bin_indices=3)


def test_value_bucket_field_empty_list_ok():
    inst = tcspc.BinIncrementClusterEvent().value(bin_indices=[])
    arr = inst.bin_indices
    assert isinstance(arr, np.ndarray)
    assert arr.size == 0
    assert arr.dtype == np.dtype(np.uint16)


def test_value_mixed_scalar_and_bucket_fields():
    inst = tcspc.HistogramArrayProgressEvent().value(
        valid_size=3, data_bucket=[1, 2, 3, 4]
    )
    assert inst.valid_size == 3
    arr = inst.data_bucket
    assert isinstance(arr, np.ndarray)
    assert list(arr) == [1, 2, 3, 4]


def test_event_instance_eq_bucket_fields():
    a = tcspc.BinIncrementClusterEvent().value(bin_indices=[1, 2, 3])
    b = tcspc.BinIncrementClusterEvent().value(bin_indices=[1, 2, 3])
    c = tcspc.BinIncrementClusterEvent().value(bin_indices=[1, 2, 4])
    assert a == b
    assert a != c
    assert a != object()


def test_event_instance_hash_raises_for_bucket_fields():
    inst = tcspc.BinIncrementClusterEvent().value(bin_indices=[1])
    with pytest.raises(TypeError, match="unhashable"):
        hash(inst)


def test_event_instance_hash_works_for_scalar_fields():
    hash(tcspc.DetectionEvent().value(abstime=1, channel=2))


def test_event_instance_repr_with_bucket_field_smoke():
    r = repr(tcspc.BinIncrementClusterEvent().value(bin_indices=[1, 2]))
    assert "BinIncrementClusterEvent" in r
    assert "bin_indices" in r


def test_bucket_field_event_instance_cpp_expression_raises():
    inst = tcspc.BinIncrementClusterEvent().value(bin_indices=[1])
    with pytest.raises(TypeError, match="Prepend or Append"):
        inst._cpp_expression()


def test_bucket_field_wrapper_class_def_substrings():
    code = tcspc.BinIncrementClusterEvent()._cpp_wrapper_class_def(
        _CppIdentifier("mod")
    )
    assert '.def_prop_rw("bin_indices"' in code
    assert "nanobind::handle_t<" in code
    assert "tcspc::new_delete_bucket_source<" in code
    assert "std::copy_n(" in code


def test_mixed_field_wrapper_class_def_substrings():
    code = tcspc.HistogramArrayProgressEvent()._cpp_wrapper_class_def(
        _CppIdentifier("mod")
    )
    assert '.def_rw("valid_size"' in code
    assert '.def_prop_rw("data_bucket"' in code


def test_bucket_field_cpp_output_handlers_substrings():
    cpp = tcspc.HistogramEvent()._cpp_type_name()
    code = tcspc.HistogramEvent()._cpp_output_handlers(_CppIdentifier("sink"))
    assert f"void handle({cpp} const &event)" in code
    assert f"void handle({cpp} &&event)" in code
    assert "share_or_copy_bucket(event.data_bucket)" in code
    assert "nanobind::cast(std::move(" in code


def test_mixed_field_cpp_output_handlers_initialize_in_schema_order():
    code = tcspc.HistogramArrayProgressEvent()._cpp_output_handlers(
        _CppIdentifier("sink")
    )
    assert ".valid_size = event.valid_size," in code
    assert ".data_bucket = share_or_copy_bucket(event.data_bucket)," in code
    assert code.index(".valid_size") < code.index(".data_bucket")


# --- ArrayEventType / DetectionPairEvent (fixed-size array events) ---


def test_DetectionPairEvent_field_schema_is_single_array_field():
    schema = tcspc.DetectionPairEvent()._field_schema()
    assert len(schema) == 1
    f = schema[0]
    assert isinstance(f, _ArrayField)
    assert f.name == "elements"
    assert f.count == 2
    assert f.element_type == tcspc.DetectionEvent()


def test_ArrayEventType_cpp_type_name():
    et = tcspc.ArrayEventType(tcspc.DetectionEvent(), 3)
    assert et._cpp_type_name() == (
        "std::array<tcspc::detection_event<tcspc::default_numeric_traits>, 3>"
    )


def test_ArrayEventType_rejects_zero_count():
    with pytest.raises(ValueError):
        tcspc.ArrayEventType(tcspc.DetectionEvent(), 0)


def test_ArrayEventType_rejects_non_value_element():
    with pytest.raises(ValueError):
        tcspc.ArrayEventType(tcspc.BucketEvent(IntEvent), 2)


def test_ArrayEventType_supports_value():
    assert tcspc.ArrayEventType(tcspc.DetectionEvent(), 2)._supports_value()
    assert tcspc.DetectionPairEvent()._supports_value()


def test_ArrayEventType_eq_depends_on_element_and_count():
    a = tcspc.ArrayEventType(tcspc.DetectionEvent(), 2)
    b = tcspc.ArrayEventType(tcspc.DetectionEvent(), 2)
    c = tcspc.ArrayEventType(tcspc.DetectionEvent(), 3)
    d = tcspc.ArrayEventType(tcspc.MarkerEvent(), 2)
    assert a == b
    assert a != c
    assert a != d


def test_DetectionPairEvent_eq_ArrayEventType():
    assert tcspc.DetectionPairEvent() == tcspc.ArrayEventType(
        tcspc.DetectionEvent(), 2
    )


def test_value_dependency_event_types():
    assert tcspc.DetectionPairEvent()._value_dependency_event_types() == [
        tcspc.DetectionEvent()
    ]
    assert tcspc.DetectionEvent()._value_dependency_event_types() == []


def _pair_value():
    start = tcspc.DetectionEvent().value(abstime=0, channel=0)
    stop = tcspc.DetectionEvent().value(abstime=10, channel=1)
    return (
        tcspc.DetectionPairEvent().value(elements=[start, stop]),
        start,
        stop,
    )


def test_DetectionPairEvent_value_happy_path():
    pair, start, stop = _pair_value()
    assert pair.elements == (start, stop)
    assert len(pair) == 2
    assert pair[0] == start
    assert pair[1] == stop


def test_DetectionPairEvent_value_wrong_length_raises():
    start = tcspc.DetectionEvent().value(abstime=0, channel=0)
    with pytest.raises(TypeError, match="exactly 2"):
        tcspc.DetectionPairEvent().value(elements=[start])


def test_DetectionPairEvent_value_non_event_instance_raises():
    start = tcspc.DetectionEvent().value(abstime=0, channel=0)
    with pytest.raises(TypeError, match="EventInstance"):
        tcspc.DetectionPairEvent().value(elements=[start, 42])  # type: ignore[arg-type]


def test_DetectionPairEvent_value_wrong_element_type_raises():
    start = tcspc.DetectionEvent().value(abstime=0, channel=0)
    wrong = tcspc.MarkerEvent().value(abstime=10, channel=1)
    with pytest.raises(TypeError, match="event type"):
        tcspc.DetectionPairEvent().value(elements=[start, wrong])


def test_DetectionPairEvent_value_eq_and_hash():
    a, _, _ = _pair_value()
    b, _, _ = _pair_value()
    assert a == b
    assert hash(a) == hash(b)

    other_start = tcspc.DetectionEvent().value(abstime=5, channel=0)
    other_stop = tcspc.DetectionEvent().value(abstime=10, channel=1)
    c = tcspc.DetectionPairEvent().value(elements=[other_start, other_stop])
    assert a != c


def test_DetectionPairEvent_value_cpp_expression():
    pair, _, _ = _pair_value()
    expr = pair._cpp_expression()
    elem = "tcspc::detection_event<tcspc::default_numeric_traits>"
    assert expr == (
        f"std::array<{elem}, 2>{{"
        f"{elem}{{"
        ".abstime = static_cast<tcspc::default_numeric_traits::abstime_type>(0), "
        ".channel = static_cast<tcspc::default_numeric_traits::channel_type>(0)}, "
        f"{elem}{{"
        ".abstime = static_cast<tcspc::default_numeric_traits::abstime_type>(10), "
        ".channel = static_cast<tcspc::default_numeric_traits::channel_type>(1)}}"
    )
