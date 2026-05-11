# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys

import numpy as np
import pytest
from libtcspc._cpp_utils import (
    cpp_type_from_dtype,
    float32_type,
    float64_type,
    int8_type,
    int16_type,
    int32_type,
    int64_type,
    uint8_type,
    uint16_type,
    uint32_type,
    uint64_type,
)


def test_accepts_unsigned_scalar_types():
    assert cpp_type_from_dtype(np.uint8) == uint8_type
    assert cpp_type_from_dtype(np.uint16) == uint16_type
    assert cpp_type_from_dtype(np.uint32) == uint32_type
    assert cpp_type_from_dtype(np.uint64) == uint64_type


def test_accepts_signed_scalar_types():
    assert cpp_type_from_dtype(np.int8) == int8_type
    assert cpp_type_from_dtype(np.int16) == int16_type
    assert cpp_type_from_dtype(np.int32) == int32_type
    assert cpp_type_from_dtype(np.int64) == int64_type


def test_accepts_float_scalar_types():
    assert cpp_type_from_dtype(np.float32) == float32_type
    assert cpp_type_from_dtype(np.float64) == float64_type


def test_accepts_dtype_objects():
    assert cpp_type_from_dtype(np.dtype("uint16")) == uint16_type
    assert cpp_type_from_dtype(np.dtype("int64")) == int64_type


def test_accepts_strings():
    assert cpp_type_from_dtype("uint16") == uint16_type
    assert cpp_type_from_dtype("int32") == int32_type
    assert cpp_type_from_dtype("float64") == float64_type


def test_rejects_bool():
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.bool_)


def test_rejects_complex():
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.complex64)
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.complex128)


def test_rejects_datetime_and_timedelta():
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.dtype("datetime64[ns]"))
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.dtype("timedelta64[ns]"))


def test_rejects_object_dtype():
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.dtype("O"))


def test_rejects_string_dtype():
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.dtype("S4"))


def test_rejects_structured_dtype():
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.dtype([("a", "i4"), ("b", "f4")]))


def test_rejects_non_native_byte_order():
    nonnative = ">u2" if sys.byteorder == "little" else "<u2"
    with pytest.raises(TypeError, match="non-native byte order"):
        cpp_type_from_dtype(np.dtype(nonnative))


def test_accepts_explicit_native_byte_order():
    native = "<u2" if sys.byteorder == "little" else ">u2"
    assert cpp_type_from_dtype(np.dtype(native)) == uint16_type


def test_rejects_unsupported_width_float16():
    with pytest.raises(TypeError, match="unsupported dtype"):
        cpp_type_from_dtype(np.float16)


def test_rejects_garbage():
    with pytest.raises(TypeError, match="not a NumPy dtype"):
        cpp_type_from_dtype(object())
