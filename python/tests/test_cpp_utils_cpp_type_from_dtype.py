# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys

import numpy as np
import pytest
from libtcspc._cpp_utils import (
    _cpp_type_from_dtype,
    _float32_type,
    _float64_type,
    _int8_type,
    _int16_type,
    _int32_type,
    _int64_type,
    _uint8_type,
    _uint16_type,
    _uint32_type,
    _uint64_type,
)


def test_accepts_unsigned_scalar_types():
    assert _cpp_type_from_dtype(np.uint8) == _uint8_type
    assert _cpp_type_from_dtype(np.uint16) == _uint16_type
    assert _cpp_type_from_dtype(np.uint32) == _uint32_type
    assert _cpp_type_from_dtype(np.uint64) == _uint64_type


def test_accepts_signed_scalar_types():
    assert _cpp_type_from_dtype(np.int8) == _int8_type
    assert _cpp_type_from_dtype(np.int16) == _int16_type
    assert _cpp_type_from_dtype(np.int32) == _int32_type
    assert _cpp_type_from_dtype(np.int64) == _int64_type


def test_accepts_float_scalar_types():
    assert _cpp_type_from_dtype(np.float32) == _float32_type
    assert _cpp_type_from_dtype(np.float64) == _float64_type


def test_accepts_dtype_objects():
    assert _cpp_type_from_dtype(np.dtype("uint16")) == _uint16_type
    assert _cpp_type_from_dtype(np.dtype("int64")) == _int64_type


def test_accepts_strings():
    assert _cpp_type_from_dtype("uint16") == _uint16_type
    assert _cpp_type_from_dtype("int32") == _int32_type
    assert _cpp_type_from_dtype("float64") == _float64_type


def test_rejects_bool():
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.bool_)


def test_rejects_complex():
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.complex64)
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.complex128)


def test_rejects_datetime_and_timedelta():
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.dtype("datetime64[ns]"))
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.dtype("timedelta64[ns]"))


def test_rejects_object_dtype():
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.dtype("O"))


def test_rejects_string_dtype():
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.dtype("S4"))


def test_rejects_structured_dtype():
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.dtype([("a", "i4"), ("b", "f4")]))


def test_rejects_non_native_byte_order():
    nonnative = ">u2" if sys.byteorder == "little" else "<u2"
    with pytest.raises(TypeError, match="non-native byte order"):
        _cpp_type_from_dtype(np.dtype(nonnative))


def test_accepts_explicit_native_byte_order():
    native = "<u2" if sys.byteorder == "little" else ">u2"
    assert _cpp_type_from_dtype(np.dtype(native)) == _uint16_type


def test_rejects_unsupported_width_float16():
    with pytest.raises(TypeError, match="unsupported dtype"):
        _cpp_type_from_dtype(np.float16)


def test_rejects_garbage():
    with pytest.raises(TypeError, match="not a NumPy dtype"):
        _cpp_type_from_dtype(object())
