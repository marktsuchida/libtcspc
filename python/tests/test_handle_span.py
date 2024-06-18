# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import array

import cppyy
import numpy as np
import pytest
from numpy.lib import NumpyVersion

cppyy.include("libtcspc_py/handle_span.hpp")

cppyy.include("libtcspc/tcspc.hpp")

cppyy.include("cstddef")
cppyy.include("cstdint")
cppyy.include("string")
cppyy.include("vector")


def test_raise_type_error_with_no_cause():
    with pytest.raises(TypeError, match="hello"):
        cppyy.gbl.tcspc.py.internal.raise_type_error("hello")


cppyy.cppdef("""\
    namespace tcspc::py::internal {
        auto raise_type_error_twice(std::string const &m0,
                                    std::string const &m1) -> PyObject * {
                raise_type_error(m0);
                return raise_type_error(m1);
        }
    }""")


def test_raise_type_error_with_cause():
    with pytest.raises(TypeError) as exc_info:
        cppyy.gbl.tcspc.py.internal.raise_type_error_twice("msg0", "msg1")
    assert exc_info.type is TypeError
    assert "msg1" in str(exc_info.value)
    # As commended in the implementation, cppyy loses the exception cause so
    # we include the original message in the new exception message.
    assert "caused by: msg0" in str(exc_info.value)


def test_is_buffer():
    is_buffer = cppyy.gbl.tcspc.py.is_buffer
    assert is_buffer(b"")
    assert is_buffer(memoryview(b""))
    assert is_buffer(array.array("i", []))
    assert is_buffer(memoryview(array.array("i", [])))
    assert not is_buffer(42)
    assert not is_buffer("")
    assert not is_buffer(None)


# Mock to serve as processor for handle_buffer().
cppyy.cppdef("""\
    namespace tcspc::py {
        template <typename T> struct mock_handler {
            bool called = false;
            std::vector<T> received;

            void reset() {
                called = false;
                received.clear();
            }

            void handle(span<T const> const &event) {
                called = true;
                received.assign(event.begin(), event.end());
            }

            // To access result as integer (avoid conversion for char, etc.)
            auto received_as_i64(std::size_t index) const -> i64 {
                return static_cast<i64>(received.at(index));
            }
        };
    }""")


def make_mock_handler(elemtype: str):
    # Workaround for https://github.com/wlav/cppyy/issues/241
    cppyy.gbl.std.vector[elemtype]
    return cppyy.gbl.tcspc.py.mock_handler[elemtype]()


@pytest.mark.parametrize("elemtype", ("std::byte", "std::uint8_t"))
def test_handle_buffer_bytes(elemtype):
    handler = make_mock_handler(elemtype)
    handle_buffer = cppyy.gbl.tcspc.py.handle_buffer[type(handler)]
    handle_buffer(handler, b"")
    assert handler.called
    assert handler.received.empty()

    handler.reset()
    handle_buffer(handler, b"abc")
    assert handler.called
    assert handler.received.size() == 3
    assert handler.received_as_i64(2) == ord("c")


@pytest.mark.parametrize("elemtype", ("signed char", "short", "int", "bool"))
def test_handle_buffer_rejects_wrong_element_type(elemtype):
    handler = make_mock_handler(elemtype)
    handle_buffer = cppyy.gbl.tcspc.py.handle_buffer[type(handler)]
    with pytest.raises(TypeError):
        handle_buffer(handler, b"")


@pytest.mark.parametrize("elemtype", ("std::byte", "std::uint8_t"))
def test_handle_buffer_memoryview(elemtype):
    handler = make_mock_handler(elemtype)
    handle_buffer = cppyy.gbl.tcspc.py.handle_buffer[type(handler)]
    handle_buffer(handler, memoryview(b"abc"))
    assert handler.called
    assert handler.received.size() == 3


_cpp_fixed_width_for_long = "tcspc::i" + str(8 * cppyy.sizeof("long"))
_cpp_fixed_width_for_ulong = "tcspc::u" + str(
    8 * cppyy.sizeof("unsigned long")
)

_cpp_fixed_width_for_np_int_ = (
    _cpp_fixed_width_for_long
    if NumpyVersion(np.__version__) < "2.0.0b1"
    else "tcspc::i" + str(8 * cppyy.sizeof("std::size_t"))
)

_cpp_fixed_width_for_np_uint = (
    _cpp_fixed_width_for_ulong
    if NumpyVersion(np.__version__) < "2.0.0b1"
    else "tcspc::u" + str(8 * cppyy.sizeof("std::size_t"))
)


@pytest.mark.parametrize(
    "typecode, elemtype",
    [
        # "c", "?", "n", "N" are not supported by array.array.
        ("b", "tcspc::i8"),
        ("B", "tcspc::u8"),
        ("B", "std::byte"),
        ("h", "tcspc::i16"),
        ("H", "tcspc::u16"),
        ("i", "tcspc::i32"),
        ("I", "tcspc::u32"),
        ("l", _cpp_fixed_width_for_long),
        ("L", _cpp_fixed_width_for_ulong),
        ("q", "tcspc::i64"),
        ("Q", "tcspc::u64"),
        ("f", "float"),
        ("d", "double"),
    ],
)
def test_handle_buffer_array_array(typecode, elemtype):
    handler = make_mock_handler(elemtype)
    handle_buffer = cppyy.gbl.tcspc.py.handle_buffer[type(handler)]
    handle_buffer(handler, array.array(typecode, [42, 43, 44]))
    assert handler.called
    assert handler.received.size() == 3
    assert handler.received_as_i64(2) == 44


# A note on Numpy scalar types and C types:
# https://numpy.org/doc/stable/user/basics.types.html
#
# The following have not changed meaning through versions and match C types:
# - np.bool_: bool
# - np.byte, np.ubyte: signed char and unsigned char
# - np.short, np.ushort: short and unsigned short
# - np.intc, np.uintc: int and unsigned
# - np.int_, np.uint: long and unsigned long (!) for numpy<2; [s]size_t for
#                     numpy>=2 (changed on Windows x64)
# - np.longlong, np.ulonglong: long long and unsigned long long
# - np.single, np.double: float and double
# - (Complex and long double types omitted for now.)
# - All the fixed-width aliases (such as np.int32): cstdint fixed-width types
#
# (Note, however, that there is no guarantee that any of these produce the
# buffer protocol type code matching the exact C type; only bit compatibility is
# guaranteed.)
#
# The following were deprecated in 1.20, removed in 1.24, and reintroduced in
# 2.0 with different meaning; best to avoid for as long as practical:
# - np.bool: used to be Python bool, now C bool (= np.bool_)
# - np.long, np.ulong: used to be int_/uint, now intc/uintc

# Here we are interested in testing that all common NumPy scalar types work
# with libtcspc processors taking spans of the compatible cstdint fixed-width
# types (and std::byte). Testing for handle_buffer() working correctly for
# every possible buffer protocol format code cannot be done using NumPy arrays.


@pytest.mark.parametrize(
    "dtype, elemtype",
    [
        (np.int8, "tcspc::i8"),
        (np.uint8, "tcspc::u8"),
        (np.uint8, "std::byte"),
        (np.int16, "tcspc::i16"),
        (np.uint16, "tcspc::u16"),
        (np.int32, "tcspc::i32"),
        (np.uint32, "tcspc::u32"),
        (np.int64, "tcspc::i64"),
        (np.uint64, "tcspc::u64"),
        (np.float32, "float"),
        (np.float64, "double"),
        (np.bool_, "bool"),
        (np.byte, "tcspc::i8"),
        (np.ubyte, "tcspc::u8"),
        (np.ubyte, "std::byte"),
        (np.short, "tcspc::i16"),
        (np.ushort, "tcspc::u16"),
        (np.intc, "tcspc::i32"),
        (np.uintc, "tcspc::u32"),
        (np.int_, _cpp_fixed_width_for_np_int_),
        (np.uint, _cpp_fixed_width_for_np_uint),
        (np.longlong, "tcspc::i64"),
        (np.ulonglong, "tcspc::u64"),
        (np.single, "float"),
        (np.double, "double"),
    ],
)
def test_handle_buffer_numpy(dtype, elemtype):
    handler = make_mock_handler(elemtype)
    handle_buffer = cppyy.gbl.tcspc.py.handle_buffer[type(handler)]
    handle_buffer(handler, np.asarray([42, 43, 44], dtype=dtype))
    assert handler.called
    assert handler.received.size() == 3
    if elemtype != "bool":
        assert handler.received_as_i64(2) == 44


def test_handle_buffer_rejects_nonnative_byteorder():
    handler = make_mock_handler("int")
    handle_buffer = cppyy.gbl.tcspc.py.handle_buffer[type(handler)]
    handle_buffer(handler, np.asarray([42, 43, 44], dtype="i4"))
    with pytest.raises(TypeError):
        handle_buffer(handler, np.asarray([42, 43, 44], dtype=">i4"))


# Since we only view buffers as 1-dimensional arrays, in theory we could allow
# Fortran-contiguous arrays. But that would probably only cause confusion, so
# we reject them. (Plus, it's complicated to allow F-contiguous while
# disallowing strided arrays.)
def test_handle_buffer_rejects_fortran_contiguous():
    handler = make_mock_handler("int")
    handle_buffer = cppyy.gbl.tcspc.py.handle_buffer[type(handler)]
    arr = np.eye(3, dtype=np.intc)
    handle_buffer(handler, arr)
    with pytest.raises(TypeError):
        handle_buffer(handler, np.asfortranarray(arr))


def test_handle_buffer_rejects_strided():
    handler = make_mock_handler("int")
    handle_buffer = cppyy.gbl.tcspc.py.handle_buffer[type(handler)]
    arr = np.eye(3, dtype=np.intc)
    handle_buffer(handler, arr[0, :])
    with pytest.raises(TypeError):
        handle_buffer(handler, arr[:, 0])
