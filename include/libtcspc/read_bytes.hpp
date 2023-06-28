/*
 * This file is part of libtcspc
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "span.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tcspc {

/**
 * \brief A span of \c std::byte.
 */
template <std::size_t Extent> using byte_span = span<std::byte const, Extent>;

/**
 * \brief Get a subspan of an array of \c std::byte.
 */
template <std::size_t Offset, std::size_t Count, std::size_t Size>
inline auto byte_subspan(std::array<std::byte, Size> const &bytes) noexcept
    -> byte_span<Count> {
    static_assert(Offset + Count <= Size);
    return byte_span<Size>(bytes).template subspan<Offset, Count>();
}

namespace internal {

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
    int i = 1;
    char c[sizeof(i)];             // NOLINT
    std::memcpy(c, &i, sizeof(i)); // NOLINT
    return c[0] != 0;
}

inline auto use_memcpy() noexcept -> bool {
#ifdef _MSC_VER
    return is_little_endian();
#else
    return false;
#endif
}

inline auto read_u16le_memcpy(byte_span<2> bytes) noexcept -> std::uint16_t {
    assert(is_little_endian());
    std::uint16_t ret{};
    std::memcpy(&ret, bytes.data(), sizeof(ret));
    return ret;
}

inline auto read_u32le_memcpy(byte_span<4> bytes) noexcept -> std::uint32_t {
    assert(is_little_endian());
    std::uint32_t ret{};
    std::memcpy(&ret, bytes.data(), sizeof(ret));
    return ret;
}

inline auto read_u64le_memcpy(byte_span<8> bytes) noexcept -> std::uint64_t {
    assert(is_little_endian());
    std::uint64_t ret{};
    std::memcpy(&ret, bytes.data(), sizeof(ret));
    return ret;
}

constexpr auto read_u16le_generic(byte_span<2> bytes) noexcept
    -> std::uint16_t {
    // unsigned is at least as wide as uint16_t
    return std::uint16_t(unsigned(bytes[0]) | (unsigned(bytes[1]) << 8));
}

constexpr auto read_u32le_generic(byte_span<4> bytes) noexcept
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

constexpr auto read_u64le_generic(byte_span<8> bytes) noexcept
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

} // namespace internal

/**
 * \brief Read an 8-bit unsigned integer from a byte.
 */
constexpr auto read_u8(byte_span<1> bytes) noexcept -> std::uint8_t {
    return std::uint8_t(bytes[0]);
}

/**
 * \brief Read an little-endian 16-bit unsigned integer from bytes.
 */
inline auto read_u16le(byte_span<2> bytes) noexcept -> std::uint16_t {
    if (internal::use_memcpy())
        return internal::read_u16le_memcpy(bytes);
    return internal::read_u16le_generic(bytes);
}

/**
 * \brief Read an little-endian 32-bit unsigned integer from bytes.
 */
inline auto read_u32le(byte_span<4> bytes) noexcept -> std::uint32_t {
    if (internal::use_memcpy())
        return internal::read_u32le_memcpy(bytes);
    return internal::read_u32le_generic(bytes);
}

/**
 * \brief Read an little-endian 64-bit unsigned integer from bytes.
 */
inline auto read_u64le(byte_span<8> bytes) noexcept -> std::uint64_t {
    if (internal::use_memcpy())
        return internal::read_u64le_memcpy(bytes);
    return internal::read_u64le_generic(bytes);
}

/**
 * \brief Read an 8-bit signed integer from a byte.
 */
inline auto read_i8(byte_span<1> bytes) noexcept -> std::int8_t {
    return static_cast<std::int8_t>(read_u8(bytes));
}

/**
 * \brief Read an little-endian 16-bit signed integer from bytes.
 */
inline auto read_i16le(byte_span<2> bytes) noexcept -> std::int16_t {
    return static_cast<std::int16_t>(read_u16le(bytes));
}

/**
 * \brief Read an little-endian 32-bit signed integer from bytes.
 */
inline auto read_i32le(byte_span<4> bytes) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(read_u32le(bytes));
}

/**
 * \brief Read an little-endian 64-bit signed integer from bytes.
 */
inline auto read_i64le(byte_span<8> bytes) noexcept -> std::int64_t {
    return static_cast<std::int64_t>(read_u64le(bytes));
}

} // namespace tcspc
