/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "int_types.hpp"
#include "npint.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <span>

namespace tcspc {

namespace internal {

// Read little-endian integers from raw bytes, regardless of source alignment.
//
// On little-endian targets we use std::bit_cast. On big-endian targets we fall
// back to a shift-based composition. The shift form is endian-agnostic and
// would produce correct results everywhere, but at the time this code was
// originally written some compilers (notably MSVC) generated poor code for it;
// std::bit_cast reliably compiles to a single load.
//
// For the shift-based composition see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html
// (note the explicit casts, which are needed to suppress integer promotions
// that would otherwise mangle the result).

constexpr auto read_u16le(std::span<std::byte const, 2> bytes) noexcept
    -> u16np {
    if constexpr (std::endian::native == std::endian::little) {
        return u16np(std::bit_cast<u16>(std::array{bytes[0], bytes[1]}));
    } else {
        return (u16np(u16(bytes[0])) << 0) | (u16np(u16(bytes[1])) << 8);
    }
}

constexpr auto read_u32le(std::span<std::byte const, 4> bytes) noexcept
    -> u32np {
    if constexpr (std::endian::native == std::endian::little) {
        return u32np(std::bit_cast<u32>(
            std::array{bytes[0], bytes[1], bytes[2], bytes[3]}));
    } else {
        return (u32np(u32(bytes[0])) << 0) | (u32np(u32(bytes[1])) << 8) |
               (u32np(u32(bytes[2])) << 16) | (u32np(u32(bytes[3])) << 24);
    }
}

constexpr auto read_u64le(std::span<std::byte const, 8> bytes) noexcept
    -> u64np {
    if constexpr (std::endian::native == std::endian::little) {
        return u64np(std::bit_cast<u64>(
            std::array{bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
                       bytes[5], bytes[6], bytes[7]}));
    } else {
        return (u64np(u64(bytes[0])) << 0) | (u64np(u64(bytes[1])) << 8) |
               (u64np(u64(bytes[2])) << 16) | (u64np(u64(bytes[3])) << 24) |
               (u64np(u64(bytes[4])) << 32) | (u64np(u64(bytes[5])) << 40) |
               (u64np(u64(bytes[6])) << 48) | (u64np(u64(bytes[7])) << 56);
    }
}

} // namespace internal

/**
 * \brief Read an 8-bit unsigned integer from bytes at offset.
 *
 * \ingroup integers
 */
template <std::size_t Offset, std::convertible_to<std::byte> T, std::size_t N>
constexpr auto read_u8_at(std::span<T, N> bytes) noexcept -> u8np {
    static_assert(Offset + 1 <= N);
    auto const s = bytes.template subspan<Offset, 1>();
    auto const b = std::span<std::byte const, 1>(s);
    return u8np(u8(b.front()));
}

/**
 * \brief Read a little-endian 16-bit unsigned integer from bytes at offset.
 *
 * \ingroup integers
 */
template <std::size_t Offset, std::convertible_to<std::byte> T, std::size_t N>
constexpr auto read_u16le_at(std::span<T, N> bytes) noexcept -> u16np {
    static_assert(Offset + 2 <= N);
    auto const s = bytes.template subspan<Offset, 2>();
    return internal::read_u16le(std::span<std::byte const, 2>(s));
}

/**
 * \brief Read a little-endian 32-bit unsigned integer from bytes at offset.
 *
 * \ingroup integers
 */
template <std::size_t Offset, std::convertible_to<std::byte> T, std::size_t N>
constexpr auto read_u32le_at(std::span<T, N> bytes) noexcept -> u32np {
    static_assert(Offset + 4 <= N);
    auto const s = bytes.template subspan<Offset, 4>();
    return internal::read_u32le(std::span<std::byte const, 4>(s));
}

/**
 * \brief Read a little-endian 64-bit unsigned integer from bytes at offset.
 *
 * \ingroup integers
 */
template <std::size_t Offset, std::convertible_to<std::byte> T, std::size_t N>
constexpr auto read_u64le_at(std::span<T, N> bytes) noexcept -> u64np {
    static_assert(Offset + 8 <= N);
    auto const s = bytes.template subspan<Offset, 8>();
    return internal::read_u64le(std::span<std::byte const, 8>(s));
}

/**
 * \brief Read an 8-bit signed integer from bytes at offset.
 *
 * \ingroup integers
 */
template <std::size_t Offset, std::convertible_to<std::byte> T, std::size_t N>
constexpr auto read_i8_at(std::span<T, N> bytes) noexcept -> i8np {
    return i8np(read_u8_at<Offset, T, N>(bytes));
}

/**
 * \brief Read a little-endian 16-bit signed integer from bytes at offset.
 *
 * \ingroup integers
 */
template <std::size_t Offset, std::convertible_to<std::byte> T, std::size_t N>
constexpr auto read_i16le_at(std::span<T, N> bytes) noexcept -> i16np {
    return i16np(read_u16le_at<Offset, T, N>(bytes));
}

/**
 * \brief Read a little-endian 32-bit signed integer from bytes at offset.
 *
 * \ingroup integers
 */
template <std::size_t Offset, std::convertible_to<std::byte> T, std::size_t N>
constexpr auto read_i32le_at(std::span<T, N> bytes) noexcept -> i32np {
    return i32np(read_u32le_at<Offset, T, N>(bytes));
}

/**
 * \brief Read a little-endian 64-bit signed integer from bytes at offset.
 *
 * \ingroup integers
 */
template <std::size_t Offset, std::convertible_to<std::byte> T, std::size_t N>
constexpr auto read_i64le_at(std::span<T, N> bytes) noexcept -> i64np {
    return i64np(read_u64le_at<Offset, T, N>(bytes));
}

} // namespace tcspc
