/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>

namespace flimevt::internal {

// Internal functions to read integers from little-endian data streams.

// We provide 2 implementations of each read_u*le() function, "_generic" and
// "_memcpy". The "_generic" way is endian-agnostic but is poorly optimized by
// some compilers, including MSVC at the time of writing. The "_memcpy" way is
// only implemented for little-endian targets (for now).

// All variants work with any (lack of) alignment.

// For the "_generic" implementations, see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html
// (but note the need for extra casts for it to actually work correctly).

inline auto is_little_endian() noexcept -> bool {
    // Note: in C++20 this can be replaced with std::endian checks, and be made
    // constexpr.
    union {
        int i;
        char c;
    } const t{1};
    return bool(t.c);
}

inline auto use_memcpy() noexcept -> bool {
#ifdef _MSC_VER
    return is_little_endian();
#else
    return false;
#endif
}

inline auto read_u16le_memcpy(unsigned char const *bytes) noexcept
    -> std::uint16_t {
    assert(is_little_endian());
    std::uint16_t ret{};
    std::memcpy(&ret, bytes, sizeof(ret));
    return ret;
}

inline auto read_u32le_memcpy(unsigned char const *bytes) noexcept
    -> std::uint32_t {
    assert(is_little_endian());
    std::uint32_t ret{};
    std::memcpy(&ret, bytes, sizeof(ret));
    return ret;
}

inline auto read_u64le_memcpy(unsigned char const *bytes) noexcept
    -> std::uint64_t {
    assert(is_little_endian());
    std::uint64_t ret{};
    std::memcpy(&ret, bytes, sizeof(ret));
    return ret;
}

constexpr auto read_u16le_generic(unsigned char const *bytes) noexcept
    -> std::uint16_t {
    // unsigned is at least as wide as uint16_t
    return std::uint16_t(unsigned(bytes[0]) | (unsigned(bytes[1]) << 8));
}

constexpr auto read_u32le_generic(unsigned char const *bytes) noexcept
    -> std::uint32_t {
    using u32 = std::uint32_t;
    if constexpr (sizeof(u32) < sizeof(unsigned)) {
        // u32 would be promoted to int if we don't cast to unsigned
        return u32((unsigned(bytes[0]) << 0) | (unsigned(bytes[1]) << 8) |
                   (unsigned(bytes[2]) << 16) | (unsigned(bytes[3]) << 24));
    } else {
        return (u32(bytes[0]) << 0) | (u32(bytes[1]) << 8) |
               (u32(bytes[2]) << 16) | (u32(bytes[3]) << 24);
    }
}

constexpr auto read_u64le_generic(unsigned char const *bytes) noexcept
    -> std::uint64_t {
    using u64 = std::uint64_t;
    if constexpr (sizeof(u64) < sizeof(unsigned)) {
        // u64 would be promoted to int if we don't cast to unsigned
        return u64((unsigned(bytes[0]) << 0) | (unsigned(bytes[1]) << 8) |
                   (unsigned(bytes[2]) << 16) | (unsigned(bytes[3]) << 24) |
                   (unsigned(bytes[4]) << 32) | (unsigned(bytes[5]) << 40) |
                   (unsigned(bytes[6]) << 48) | (unsigned(bytes[7]) << 56));
    } else {
        return (u64(bytes[0]) << 0) | (u64(bytes[1]) << 8) |
               (u64(bytes[2]) << 16) | (u64(bytes[3]) << 24) |
               (u64(bytes[4]) << 32) | (u64(bytes[5]) << 40) |
               (u64(bytes[6]) << 48) | (u64(bytes[7]) << 56);
    }
}

// For completeness
constexpr auto read_u8le(unsigned char const *bytes) noexcept -> std::uint8_t {
    return bytes[0];
}

inline auto read_u16le(unsigned char const *bytes) noexcept -> std::uint16_t {
    if (use_memcpy())
        return read_u16le_memcpy(bytes);
    return read_u16le_generic(bytes);
}

inline auto read_u32le(unsigned char const *bytes) noexcept -> std::uint32_t {
    if (use_memcpy())
        return read_u32le_memcpy(bytes);
    return read_u32le_generic(bytes);
}

inline auto read_u64le(unsigned char const *bytes) noexcept -> std::uint64_t {
    if (use_memcpy())
        return read_u64le_memcpy(bytes);
    return read_u64le_generic(bytes);
}

inline auto read_i8le(unsigned char const *bytes) noexcept -> std::int8_t {
    return std::int8_t(read_u8le(bytes));
}

inline auto read_i16le(unsigned char const *bytes) noexcept -> std::int16_t {
    return std::int16_t(read_u16le(bytes));
}

inline auto read_i32le(unsigned char const *bytes) noexcept -> std::int32_t {
    return std::int32_t(read_u32le(bytes));
}

inline auto read_i64le(unsigned char const *bytes) noexcept -> std::int64_t {
    return std::int64_t(read_u64le(bytes));
}

} // namespace flimevt::internal
