# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys

import libtcspc as tcspc
import numpy as np
import pytest


def test_default_construction():
    name = tcspc.DataTypes()._cpp_type_name()
    assert name.startswith("tcspc::parameterized_data_types<")
    for slot in (
        "abstime",
        "channel",
        "difftime",
        "count",
        "datapoint",
        "bin_index",
        "bin",
    ):
        assert f"tcspc::default_data_types::{slot}_type" in name
    # Documented order.
    order = [
        "tcspc::default_data_types::abstime_type",
        "tcspc::default_data_types::channel_type",
        "tcspc::default_data_types::difftime_type",
        "tcspc::default_data_types::count_type",
        "tcspc::default_data_types::datapoint_type",
        "tcspc::default_data_types::bin_index_type",
        "tcspc::default_data_types::bin_type",
    ]
    positions = [name.index(s) for s in order]
    assert positions == sorted(positions)


def test_per_slot_override_scalar():
    name = tcspc.DataTypes(channel_type=np.uint32)._cpp_type_name()
    assert "std::uint32_t" in name
    for slot in (
        "abstime",
        "difftime",
        "count",
        "datapoint",
        "bin_index",
        "bin",
    ):
        assert f"tcspc::default_data_types::{slot}_type" in name
    assert "tcspc::default_data_types::channel_type" not in name


def test_per_slot_override_dtype_object():
    name = tcspc.DataTypes(abstime_type=np.dtype("int64"))._cpp_type_name()
    assert "std::int64_t" in name
    assert "tcspc::default_data_types::abstime_type" not in name


def test_per_slot_override_string():
    name = tcspc.DataTypes(count_type="uint16")._cpp_type_name()
    assert "std::uint16_t" in name
    assert "tcspc::default_data_types::count_type" not in name


def test_difftime_accepts_signed_integer():
    name = tcspc.DataTypes(difftime_type=np.int32)._cpp_type_name()
    assert "std::int32_t" in name
    name = tcspc.DataTypes(difftime_type=np.int64)._cpp_type_name()
    assert "std::int64_t" in name


def test_difftime_rejects_unsigned():
    with pytest.raises(TypeError, match="difftime_type"):
        tcspc.DataTypes(difftime_type=np.uint32)
    with pytest.raises(TypeError, match="difftime_type"):
        tcspc.DataTypes(difftime_type=np.uint64)


def test_difftime_rejects_float():
    with pytest.raises(TypeError, match="difftime_type"):
        tcspc.DataTypes(difftime_type=np.float32)


def test_difftime_rejects_bool():
    with pytest.raises(TypeError):
        tcspc.DataTypes(difftime_type=np.bool_)


def test_bin_index_accepts_unsigned_integer():
    for t in (np.uint8, np.uint16, np.uint32, np.uint64):
        name = tcspc.DataTypes(bin_index_type=t)._cpp_type_name()
        assert "tcspc::default_data_types::bin_index_type" not in name


def test_bin_index_rejects_signed():
    with pytest.raises(TypeError, match="bin_index_type"):
        tcspc.DataTypes(bin_index_type=np.int16)


def test_bin_index_rejects_float():
    with pytest.raises(TypeError, match="bin_index_type"):
        tcspc.DataTypes(bin_index_type=np.float32)


def test_bin_index_rejects_bool():
    with pytest.raises(TypeError):
        tcspc.DataTypes(bin_index_type=np.bool_)


@pytest.mark.parametrize(
    "slot",
    ["abstime", "channel", "count", "datapoint", "bin"],
)
def test_integer_slot_accepts_signed_and_unsigned(slot):
    key = f"{slot}_type"
    name = tcspc.DataTypes(**{key: np.int32})._cpp_type_name()
    assert "std::int32_t" in name
    name = tcspc.DataTypes(**{key: np.uint32})._cpp_type_name()
    assert "std::uint32_t" in name


@pytest.mark.parametrize(
    "slot",
    ["abstime", "channel", "count", "datapoint", "bin"],
)
def test_integer_slot_rejects_float(slot):
    key = f"{slot}_type"
    with pytest.raises(TypeError, match=key):
        tcspc.DataTypes(**{key: np.float32})


@pytest.mark.parametrize(
    "slot",
    ["abstime", "channel", "count", "datapoint", "bin"],
)
def test_integer_slot_rejects_bool(slot):
    key = f"{slot}_type"
    with pytest.raises(TypeError):
        tcspc.DataTypes(**{key: np.bool_})


@pytest.mark.parametrize(
    "slot",
    ["abstime", "channel", "count", "datapoint", "bin"],
)
def test_integer_slot_rejects_complex(slot):
    key = f"{slot}_type"
    with pytest.raises(TypeError):
        tcspc.DataTypes(**{key: np.complex64})


def test_rejects_non_native_byte_order():
    nonnative = ">u4" if sys.byteorder == "little" else "<u4"
    with pytest.raises(TypeError, match="non-native byte order"):
        tcspc.DataTypes(abstime_type=np.dtype(nonnative))


def test_rejects_structured_dtype():
    with pytest.raises(TypeError, match="unsupported dtype"):
        tcspc.DataTypes(abstime_type=np.dtype([("a", "i4"), ("b", "f4")]))


def test_rejects_unsupported_width():
    with pytest.raises(TypeError, match="unsupported dtype"):
        tcspc.DataTypes(abstime_type=np.float16)


def test_type_constants_not_publicly_accessible():
    with pytest.raises(AttributeError):
        getattr(tcspc, "uint32_type")  # noqa: B009


def test_type_constants_still_in_private_module():
    from libtcspc._cpp_utils import (  # noqa: F401
        byte_type,
        float32_type,
        float64_type,
        int8_type,
        int16_type,
        int32_type,
        int64_type,
        size_type,
        string_type,
        uint8_type,
        uint16_type,
        uint32_type,
        uint64_type,
    )
