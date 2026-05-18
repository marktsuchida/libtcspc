# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys

import libtcspc as tcspc
import numpy as np
import pytest


def test_default_construction():
    name = tcspc.NumericTraits()._cpp_type_name()
    assert name.startswith("tcspc::parameterized_numeric_traits<")
    for slot in (
        "abstime",
        "channel",
        "difftime",
        "count",
        "datapoint",
        "bin_index",
        "bin",
    ):
        assert f"tcspc::default_numeric_traits::{slot}_type" in name
    # Documented order.
    order = [
        "tcspc::default_numeric_traits::abstime_type",
        "tcspc::default_numeric_traits::channel_type",
        "tcspc::default_numeric_traits::difftime_type",
        "tcspc::default_numeric_traits::count_type",
        "tcspc::default_numeric_traits::datapoint_type",
        "tcspc::default_numeric_traits::bin_index_type",
        "tcspc::default_numeric_traits::bin_type",
    ]
    positions = [name.index(s) for s in order]
    assert positions == sorted(positions)


def test_per_slot_override_scalar():
    name = tcspc.NumericTraits(channel_type=np.uint32)._cpp_type_name()
    assert "std::uint32_t" in name
    for slot in (
        "abstime",
        "difftime",
        "count",
        "datapoint",
        "bin_index",
        "bin",
    ):
        assert f"tcspc::default_numeric_traits::{slot}_type" in name
    assert "tcspc::default_numeric_traits::channel_type" not in name


def test_per_slot_override_dtype_object():
    name = tcspc.NumericTraits(abstime_type=np.dtype("int64"))._cpp_type_name()
    assert "std::int64_t" in name
    assert "tcspc::default_numeric_traits::abstime_type" not in name


def test_per_slot_override_string():
    name = tcspc.NumericTraits(count_type="uint16")._cpp_type_name()
    assert "std::uint16_t" in name
    assert "tcspc::default_numeric_traits::count_type" not in name


def test_difftime_accepts_signed_integer():
    name = tcspc.NumericTraits(difftime_type=np.int32)._cpp_type_name()
    assert "std::int32_t" in name
    name = tcspc.NumericTraits(difftime_type=np.int64)._cpp_type_name()
    assert "std::int64_t" in name


def test_difftime_rejects_unsigned():
    with pytest.raises(TypeError, match="difftime_type"):
        tcspc.NumericTraits(difftime_type=np.uint32)
    with pytest.raises(TypeError, match="difftime_type"):
        tcspc.NumericTraits(difftime_type=np.uint64)


def test_difftime_rejects_float():
    with pytest.raises(TypeError, match="difftime_type"):
        tcspc.NumericTraits(difftime_type=np.float32)


def test_difftime_rejects_bool():
    with pytest.raises(TypeError):
        tcspc.NumericTraits(difftime_type=np.bool_)


def test_bin_index_accepts_unsigned_integer():
    for t in (np.uint8, np.uint16, np.uint32, np.uint64):
        name = tcspc.NumericTraits(bin_index_type=t)._cpp_type_name()
        assert "tcspc::default_numeric_traits::bin_index_type" not in name


def test_bin_index_rejects_signed():
    with pytest.raises(TypeError, match="bin_index_type"):
        tcspc.NumericTraits(bin_index_type=np.int16)


def test_bin_index_rejects_float():
    with pytest.raises(TypeError, match="bin_index_type"):
        tcspc.NumericTraits(bin_index_type=np.float32)


def test_bin_index_rejects_bool():
    with pytest.raises(TypeError):
        tcspc.NumericTraits(bin_index_type=np.bool_)


@pytest.mark.parametrize(
    "slot",
    ["abstime", "channel", "count", "datapoint", "bin"],
)
def test_integer_slot_accepts_signed_and_unsigned(slot):
    key = f"{slot}_type"
    name = tcspc.NumericTraits(**{key: np.int32})._cpp_type_name()
    assert "std::int32_t" in name
    name = tcspc.NumericTraits(**{key: np.uint32})._cpp_type_name()
    assert "std::uint32_t" in name


@pytest.mark.parametrize(
    "slot",
    ["abstime", "channel", "count", "datapoint", "bin"],
)
def test_integer_slot_rejects_float(slot):
    key = f"{slot}_type"
    with pytest.raises(TypeError, match=key):
        tcspc.NumericTraits(**{key: np.float32})


@pytest.mark.parametrize(
    "slot",
    ["abstime", "channel", "count", "datapoint", "bin"],
)
def test_integer_slot_rejects_bool(slot):
    key = f"{slot}_type"
    with pytest.raises(TypeError):
        tcspc.NumericTraits(**{key: np.bool_})


@pytest.mark.parametrize(
    "slot",
    ["abstime", "channel", "count", "datapoint", "bin"],
)
def test_integer_slot_rejects_complex(slot):
    key = f"{slot}_type"
    with pytest.raises(TypeError):
        tcspc.NumericTraits(**{key: np.complex64})


def test_rejects_non_native_byte_order():
    nonnative = ">u4" if sys.byteorder == "little" else "<u4"
    with pytest.raises(TypeError, match="non-native byte order"):
        tcspc.NumericTraits(abstime_type=np.dtype(nonnative))


def test_rejects_structured_dtype():
    with pytest.raises(TypeError, match="unsupported dtype"):
        tcspc.NumericTraits(abstime_type=np.dtype([("a", "i4"), ("b", "f4")]))


def test_rejects_unsupported_width():
    with pytest.raises(TypeError, match="unsupported dtype"):
        tcspc.NumericTraits(abstime_type=np.float16)


def test_type_constants_not_publicly_accessible():
    with pytest.raises(AttributeError):
        getattr(tcspc, "_uint32_type")  # noqa: B009


def test_type_constants_still_in_private_module():
    from libtcspc._cpp_utils import (  # noqa: F401
        _byte_type,
        _float32_type,
        _float64_type,
        _int8_type,
        _int16_type,
        _int32_type,
        _int64_type,
        _size_type,
        _string_type,
        _uint8_type,
        _uint16_type,
        _uint32_type,
        _uint64_type,
    )
