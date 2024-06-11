/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/span.hpp"

#include <Python.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>

// Disable IWYU to avoid false positives for Python symbols.
// NOLINTBEGIN(misc-include-cleaner)

// cppyy can translate pyobj to T* if pyobj implements the buffer protocol
// appropriately. But there are some caveats, including that empty buffers
// don't work (as of cppyy 3.1.2). So it's simpler to directly do the
// translation, despite the need to map the scalar types.

namespace tcspc::py {

namespace internal {

// Raise TypeError, preserving information from any already-raised error.
// It should be noted that cppyy may catch and handle TypeError, potentially
// trying other overloads depending on the situation. This is currently not an
// issue for us, because we raise TypeError only before actually calling into
// libtcspc C++ code.
inline auto raise_type_error(std::string msg) -> PyObject * {
    // Ideally we would set the TypeError's __cause__ to the currently raised
    // exception. But cppyy appears to erase the cause, so instead we just
    // include the original message (which is good enough for our current
    // usage).
#if PY_VERSION_HEX < 0x03120000
    // PyErr_Fetch() and PyErr_NormalizeException() are deprecated since Python
    // 3.12.
    PyObject *cause_type{};
    PyObject *cause_value{};
    PyObject *cause_tb{};
    PyErr_Fetch(&cause_type, &cause_value, &cause_tb);
    PyErr_NormalizeException(&cause_type, &cause_value, &cause_tb);
    Py_XDECREF(cause_type);
    Py_XDECREF(cause_tb);
#else
    PyObject *cause_value = PyErr_GetRaisedException();
#endif
    if (cause_value != nullptr && cause_value != Py_None) {
        PyObject *cause_msg = PyObject_Str(cause_value);
        if (cause_msg != nullptr) {
            msg += " (caused by: ";
            msg += PyUnicode_AsUTF8(cause_msg);
            msg += ")";
            Py_DECREF(cause_msg);
        } else {
            PyErr_Clear();
        }
    }
    Py_XDECREF(cause_value);

    PyErr_SetString(PyExc_TypeError, msg.c_str());
    return nullptr;
}

// Helper: handle span with known element type.
template <typename T, typename Proc>
auto handle_span(Proc &proc, void const *buf, Py_ssize_t len_bytes,
                 std::string const &type_name) -> PyObject * {
    if constexpr (handles_event_v<Proc, span<T const>>) {
        proc.handle(
            span<T const>(static_cast<T const *>(buf),
                          static_cast<std::size_t>(len_bytes) / sizeof(T)));
        Py_RETURN_NONE;
    } else {
        return raise_type_error("processor does not handle span of " +
                                type_name);
    }
}

// Helper: handle Python buffer given C element type.
template <typename CType, typename Proc>
auto handle_buffer(Proc &proc, void const *buf,
                   Py_ssize_t len_bytes) -> PyObject * {
    if constexpr (std::is_same_v<CType, double>) {
        return handle_span<double>(proc, buf, len_bytes, "double");
    } else if constexpr (std::is_same_v<CType, float>) {
        return handle_span<float>(proc, buf, len_bytes, "float");
    } else if constexpr (std::is_same_v<CType, bool>) {
        return handle_span<bool>(proc, buf, len_bytes, "bool");
    } else if constexpr (not std::is_integral_v<CType> ||
                         sizeof(CType) > sizeof(i64)) {
        return raise_type_error(
            "internal error (incorrect buffer-to-span type mapping)");
    } else if constexpr (sizeof(CType) == sizeof(i64)) {
        // For integer types, libtcspc practice is to always use the cstding
        // fixed width integer types for numerical data. We do not support the
        // conventional C integer types that are not aliased as one of those.
        if constexpr (std::is_signed_v<CType>)
            return handle_span<i64>(proc, buf, len_bytes, "int64");
        else
            return handle_span<u64>(proc, buf, len_bytes, "uint64");
    } else if constexpr (sizeof(CType) == sizeof(i32)) {
        if constexpr (std::is_signed_v<CType>)
            return handle_span<i32>(proc, buf, len_bytes, "int32");
        else
            return handle_span<u32>(proc, buf, len_bytes, "uint32");
    } else if constexpr (sizeof(CType) == sizeof(i16)) {
        if constexpr (std::is_signed_v<CType>)
            return handle_span<i16>(proc, buf, len_bytes, "int16");
        else
            return handle_span<u16>(proc, buf, len_bytes, "uint16");
    } else if constexpr (sizeof(CType) == sizeof(i8)) {
        // Exception: unsigned 8-bit could map to u8 or std::byte. We
        // support processors that handle either, and assume that
        // processors that handle both will handle them the same way.
        // Let's try std::byte first.
        if constexpr (std::is_signed_v<CType>)
            return handle_span<i8>(proc, buf, len_bytes, "int8");
        else if constexpr (handles_event_v<Proc, span<std::byte const>>)
            return handle_span<std::byte>(proc, buf, len_bytes, "byte");
        else if constexpr (handles_event_v<Proc, span<u8 const>>)
            return handle_span<u8>(proc, buf, len_bytes, "uint8");
        else
            return raise_type_error(
                "processor does not handle span of byte or uint8");
    }
}

// Simplified from std::experimental::scope_exit
template <typename Func> class scope_exit {
    Func fun;

  public:
    explicit scope_exit(Func &&func) : fun(std::move(func)) {}

    ~scope_exit() { fun(); }

    scope_exit(scope_exit const &) = delete;
    auto operator=(scope_exit const &) = delete;
    scope_exit(scope_exit &&) = delete;
    auto operator=(scope_exit &&) = delete;
};

} // namespace internal

// Separate this check from handle_buffer() so that the latter does not need to
// be instantiated unnecessarily.
inline auto is_buffer(PyObject *pyobj) -> bool {
    return PyObject_CheckBuffer(pyobj) == 1;
}

// Call proc.handle() with the span (of const) of the given pyobj.
// Raise TypeError if the buffer is incompatible.
template <typename Proc>
auto handle_buffer(Proc &proc, PyObject *pyobj) -> PyObject * {
    Py_buffer bufinfo;
    std::memset(&bufinfo, 0, sizeof(bufinfo));
    if (PyObject_GetBuffer(pyobj, &bufinfo, PyBUF_ND | PyBUF_FORMAT) != 0) {
        return internal::raise_type_error(
            "libtcspc cannot handle this buffer (a C-contiguous buffer with no strides or suboffsets is required)");
    }
    internal::scope_exit defer_release([&] { PyBuffer_Release(&bufinfo); });

    void const *const buf = bufinfo.buf;
    auto const len = bufinfo.len;
    std::string const format = bufinfo.format;

    // Format must be a single letter, optionally prefixed with '@', to
    // indicate scalar with native byte order and alignment.
    if (format.size() > 2 || (format.size() == 2 && format[0] != '@')) {
        return internal::raise_type_error(
            "libtcspc cannot handle buffer with format '" + format + "'");
    }

    switch (format[format.size() - 1]) {
    case 'c':
        return internal::handle_buffer<char>(proc, buf, len);
    case 'b':
        return internal::handle_buffer<signed char>(proc, buf, len);
    case 'B':
        return internal::handle_buffer<unsigned char>(proc, buf, len);
    case '?':
        return internal::handle_buffer<bool>(proc, buf, len);
    case 'h':
        return internal::handle_buffer<short>(proc, buf, len);
    case 'H':
        return internal::handle_buffer<unsigned short>(proc, buf, len);
    case 'i':
        return internal::handle_buffer<int>(proc, buf, len);
    case 'I':
        return internal::handle_buffer<unsigned>(proc, buf, len);
    case 'l':
        return internal::handle_buffer<long>(proc, buf, len);
    case 'L':
        return internal::handle_buffer<unsigned long>(proc, buf, len);
    case 'q':
        return internal::handle_buffer<long long>(proc, buf, len);
    case 'Q':
        return internal::handle_buffer<unsigned long long>(proc, buf, len);
    case 'n':
        return internal::handle_buffer<std::make_signed_t<std::size_t>>(
            proc, buf, len);
    case 'N':
        return internal::handle_buffer<std::size_t>(proc, buf, len);
    case 'f':
        return internal::handle_buffer<float>(proc, buf, len);
    case 'd':
        return internal::handle_buffer<double>(proc, buf, len);
    default: // float16, char[], and void * not supported.
        return internal::raise_type_error(
            "libtcspc cannot handle buffer with format '" + format + "'");
    }
}

} // namespace tcspc::py

// NOLINTEND(misc-include-cleaner)
