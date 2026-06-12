# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import sys

import libtcspc as tcspc
import numpy as np
import pytest
from libtcspc._cpp_utils import _cpp_type_from_dtype, _run_cpp_prog
from libtcspc._numeric_traits import (
    _DEFAULT_DTYPES,
    _SLOT_RULES,
    _collecting_referenced_traits,
)


def test_default_construction():
    name = tcspc.NumericTraits()._cpp_type_name()
    assert name == "tcspc::default_numeric_traits"


def test_default_construction_does_not_register_struct():
    with _collecting_referenced_traits() as registry:
        tcspc.NumericTraits()._cpp_type_name()
    assert registry == {}


def test_override_returns_hashed_struct_name():
    name = tcspc.NumericTraits(channel_type=np.uint32)._cpp_type_name()
    assert name.startswith("nt_")
    # The hash prefix is 16 hex chars after "nt_".
    assert len(name) == len("nt_") + 16


def test_override_name_is_deterministic():
    name1 = tcspc.NumericTraits(abstime_type=np.uint32)._cpp_type_name()
    name2 = tcspc.NumericTraits(abstime_type=np.uint32)._cpp_type_name()
    assert name1 == name2


def test_different_overrides_yield_different_names():
    name1 = tcspc.NumericTraits(abstime_type=np.uint32)._cpp_type_name()
    name2 = tcspc.NumericTraits(abstime_type=np.uint64)._cpp_type_name()
    assert name1 != name2


def test_struct_definition_registered_on_use():
    nt = tcspc.NumericTraits(abstime_type=np.uint32)
    with _collecting_referenced_traits() as registry:
        name = nt._cpp_type_name()
    assert name in registry
    definition = registry[name]
    assert f"struct {name} : tcspc::default_numeric_traits {{" in definition
    assert "using abstime_type = std::uint32_t;" in definition


def test_struct_definition_registered_only_for_overridden_slots():
    nt = tcspc.NumericTraits(channel_type=np.uint32)
    with _collecting_referenced_traits() as registry:
        name = nt._cpp_type_name()
    definition = registry[name]
    assert "using channel_type = std::uint32_t;" in definition
    for slot in (
        "abstime_type",
        "difftime_type",
        "count_type",
        "datapoint_type",
        "bin_index_type",
        "bin_type",
    ):
        assert f"using {slot} =" not in definition


def test_override_dtype_object():
    nt = tcspc.NumericTraits(abstime_type=np.dtype("int64"))
    with _collecting_referenced_traits() as registry:
        name = nt._cpp_type_name()
    assert "using abstime_type = std::int64_t;" in registry[name]


def test_override_string():
    nt = tcspc.NumericTraits(count_type="uint16")
    with _collecting_referenced_traits() as registry:
        name = nt._cpp_type_name()
    assert "using count_type = std::uint16_t;" in registry[name]


def test_difftime_accepts_signed_integer():
    nt = tcspc.NumericTraits(difftime_type=np.int32)
    with _collecting_referenced_traits() as registry:
        name = nt._cpp_type_name()
    assert "using difftime_type = std::int32_t;" in registry[name]

    nt = tcspc.NumericTraits(difftime_type=np.int64)
    with _collecting_referenced_traits() as registry:
        name = nt._cpp_type_name()
    assert "using difftime_type = std::int64_t;" in registry[name]


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
        nt = tcspc.NumericTraits(bin_index_type=t)
        with _collecting_referenced_traits() as registry:
            name = nt._cpp_type_name()
        assert "using bin_index_type =" in registry[name]


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
    nt = tcspc.NumericTraits(**{key: np.int32})
    with _collecting_referenced_traits() as registry:
        name = nt._cpp_type_name()
    assert f"using {key} = std::int32_t;" in registry[name]

    nt = tcspc.NumericTraits(**{key: np.uint32})
    with _collecting_referenced_traits() as registry:
        name = nt._cpp_type_name()
    assert f"using {key} = std::uint32_t;" in registry[name]


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


def test_member_dtype_defaults_match_cpp_default_numeric_traits():
    # _DEFAULT_DTYPES must mirror tcspc::default_numeric_traits; verify
    # against the C++ ground truth with a single compiled program.
    assert set(_DEFAULT_DTYPES) == {f"{c}_type" for c in _SLOT_RULES}
    nt = tcspc.NumericTraits()
    checks = " &&\n        ".join(
        f"std::is_same_v<tcspc::default_numeric_traits::{member}, "
        f"{_cpp_type_from_dtype(nt._member_dtype(member))}>"
        for member in _DEFAULT_DTYPES
    )
    assert (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>

            int main() {{
                constexpr bool result =
                    {checks};
                return result ? 1 : 0;
            }}
            """)
        == 1
    )


def test_member_dtype_overrides():
    nt = tcspc.NumericTraits(bin_type=np.uint32, abstime_type="uint64")
    assert nt._member_dtype("bin_type") == np.dtype(np.uint32)
    assert nt._member_dtype("abstime_type") == np.dtype(np.uint64)
    assert nt._member_dtype("bin_index_type") == np.dtype(np.uint16)
