/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <type_traits>

namespace tcspc {

/**
 * \brief Non-promoted integers.
 *
 * Objects of this type behave similarly to the underlying integer type, except
 * that no integer promotion is applied automatically and now implicit
 * conversion is performed to or from any other type (including boolean).
 *
 * \tparam T underlying (scalar) integer type
 */
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
class npint {
    T v;

  public:
    /** \brief The underlying (scalar) integer type. */
    using underlying_type = T;

    // Rule of zero

    /**
     * \brief Default constructor.
     *
     * Note that the value is not zero-initialized, as with regular integer
     * types (this is necessary for \c npint to be a trivial type). Brace
     * initialization can be used to zero initialize.
     */
    constexpr npint() noexcept = default;

    /**
     * \brief Construct from a value of the underlying integer type.
     *
     * \param value the value
     */
    explicit constexpr npint(T value) noexcept : v(value) {}

    /**
     * \brief Explicit conversion operator to the underlying integer type.
     */
    explicit constexpr operator T() const noexcept { return v; }

    /**
     * \brief Get the value in the underlying integer type.
     *
     * \return the value
     */
    [[nodiscard]] constexpr auto value() const noexcept -> T { return v; }

    /**
     * \brief Construct from an \c npint with a different underlying integer
     * type.
     *
     * Conversions that would both widen the integer and change the signedness
     * are prohibited, because they would make it ambiguous whether a signed or
     * unsigned extension is desired.
     *
     * \tparam U underlying integer type of source
     *
     * \param other source
     */
    template <typename U,
              typename = std::enable_if_t<std::negation_v<std::is_same<T, U>>>>
    explicit constexpr npint(npint<U> const &other) noexcept
        : v(other.value()) {
        static_assert(
            sizeof(T) <= sizeof(U) ||
                std::is_unsigned_v<T> == std::is_unsigned_v<U>,
            "Widening and changing signedness at the same time is not allowed");
    }

    // Operators

    // The following operators are intentionally left out:
    // ! && ||
    // because we do not want to automatically treat a number as a boolean.

    // Increment and decrement operators

    /** \brief Prefix increment operator. */
    constexpr auto operator++() noexcept -> npint { return npint(++v); }

    /** \brief Postfix increment operator. */
    constexpr auto operator++(int) noexcept -> npint { return npint(v++); }

    /** \brief Prefix decrement operator. */
    constexpr auto operator--() noexcept -> npint { return npint(--v); }

    /** \brief Postfix decrement operator. */
    constexpr auto operator--(int) noexcept -> npint { return npint(v--); }

    // Compound assignment operators

    /** \brief Addition asignment operator. */
    constexpr auto operator+=(npint rhs) noexcept -> npint & {
        v += rhs.v;
        return *this;
    }

    /** \brief Subtraction asignment operator. */
    constexpr auto operator-=(npint rhs) noexcept -> npint & {
        v -= rhs.v;
        return *this;
    }

    /** \brief Multiplication asignment operator. */
    constexpr auto operator*=(npint rhs) noexcept -> npint & {
        v *= rhs.v;
        return *this;
    }

    /** \brief Division asignment operator. */
    constexpr auto operator/=(npint rhs) noexcept -> npint & {
        v /= rhs.v;
        return *this;
    }

    /** \brief Remainder asignment operator. */
    constexpr auto operator%=(npint rhs) noexcept -> npint & {
        v %= rhs.v;
        return *this;
    }

    /** \brief Bitwise AND asignment operator. */
    constexpr auto operator&=(npint rhs) noexcept -> npint & {
        v &= rhs.v;
        return *this;
    }

    /** \brief Bitwise OR asignment operator. */
    constexpr auto operator|=(npint rhs) noexcept -> npint & {
        v |= rhs.v;
        return *this;
    }

    /** \brief Bitwise XOR asignment operator. */
    constexpr auto operator^=(npint rhs) noexcept -> npint & {
        v ^= rhs.v;
        return *this;
    }

    /** \brief Bitwise right shift assignment operator. */
    template <typename U>
    constexpr auto operator>>=(npint<U> rhs) noexcept -> npint & {
        v >>= rhs.value();
        return *this;
    }

    /** \brief Bitwise left shift assignment operator. */
    template <typename U>
    constexpr auto operator<<=(npint<U> rhs) noexcept -> npint & {
        v <<= rhs.value();
        return *this;
    }

    /** \brief Bitwise right shift assignment operator. */
    template <typename U, typename = std::enable_if_t<std::is_integral_v<U>>>
    constexpr auto operator>>=(U rhs) noexcept -> npint & {
        v >>= rhs;
        return *this;
    }

    /** \brief Bitwise left shift assignment operator. */
    template <typename U, typename = std::enable_if_t<std::is_integral_v<U>>>
    constexpr auto operator<<=(U rhs) noexcept -> npint & {
        v <<= rhs;
        return *this;
    }

    // Unary arighmetic operators

    /** \brief Unary plus operator. */
    constexpr auto operator+() const noexcept -> npint { return *this; }

    /** \brief Unary minus operator. */
    constexpr auto operator-() const noexcept -> npint { return npint(-v); }

    // Binary arithmetic operators

    /** \brief Addition operator. */
    friend constexpr auto operator+(npint lhs, npint rhs) noexcept -> npint {
        return lhs += rhs;
    }

    /** \brief Subtraction operator. */
    friend constexpr auto operator-(npint lhs, npint rhs) noexcept -> npint {
        return lhs -= rhs;
    }

    /** \brief Multiplication operator. */
    friend constexpr auto operator*(npint lhs, npint rhs) noexcept -> npint {
        return lhs *= rhs;
    }

    /** \brief Division operator. */
    friend constexpr auto operator/(npint lhs, npint rhs) noexcept -> npint {
        return lhs /= rhs;
    }

    /** \brief Remainder operator. */
    friend constexpr auto operator%(npint lhs, npint rhs) noexcept -> npint {
        return lhs %= rhs;
    }

    // Bitwise arithmetic operators

    /** \brief Bitwise AND operator. */
    friend constexpr auto operator&(npint lhs, npint rhs) noexcept -> npint {
        return lhs &= rhs;
    }

    /** \brief Bitwise OR operator. */
    friend constexpr auto operator|(npint lhs, npint rhs) noexcept -> npint {
        return lhs |= rhs;
    }

    /** \brief Bitwise XOR operator. */
    friend constexpr auto operator^(npint lhs, npint rhs) noexcept -> npint {
        return lhs ^= rhs;
    }

    /** \brief Bitwise NOT operator. */
    constexpr auto operator~() const noexcept -> npint { return npint(~v); }

    /** \brief Bitwise left shift operator. */
    template <typename U>
    friend constexpr auto operator<<(npint lhs, npint<U> rhs) noexcept
        -> npint {
        return lhs <<= rhs;
    }

    /** \brief Bitwise right shift operator. */
    template <typename U>
    friend constexpr auto operator>>(npint lhs, npint<U> rhs) noexcept
        -> npint {
        return lhs >>= rhs;
    }

    /** \brief Bitwise left shift operator. */
    template <typename U, typename = std::enable_if_t<std::is_integral_v<U>>>
    friend constexpr auto operator<<(npint lhs, U rhs) noexcept -> npint {
        return lhs <<= rhs;
    }

    /** \brief Bitwise right shift operator. */
    template <typename U, typename = std::enable_if_t<std::is_integral_v<U>>>
    friend constexpr auto operator>>(npint lhs, U rhs) noexcept -> npint {
        return lhs >>= rhs;
    }

    // Binary comparison operators

    /** \brief Equal to operator. */
    friend constexpr auto operator==(npint lhs, npint rhs) noexcept -> bool {
        return lhs.v == rhs.v;
    }

    /** \brief Not equal to operator. */
    friend constexpr auto operator!=(npint lhs, npint rhs) noexcept -> bool {
        return lhs.v != rhs.v;
    }

    /** \brief Less than operator. */
    friend constexpr auto operator<(npint lhs, npint rhs) noexcept -> bool {
        return lhs.v < rhs.v;
    }

    /** \brief Greater than operator. */
    friend constexpr auto operator>(npint lhs, npint rhs) noexcept -> bool {
        return lhs.v > rhs.v;
    }

    /** \brief Less than or equal to operator. */
    friend constexpr auto operator<=(npint lhs, npint rhs) noexcept -> bool {
        return lhs.v <= rhs.v;
    }

    /** \brief Greater than or equal to operator. */
    friend constexpr auto operator>=(npint lhs, npint rhs) noexcept -> bool {
        return lhs.v >= rhs.v;
    }

    // Stream insertion and extraction operators

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, npint rhs) -> std::ostream & {
        return strm << rhs.v;
    }

    /** \brief Stream extraction operator. */
    friend auto operator>>(std::istream &strm, npint &rhs) -> std::istream & {
        return strm >> rhs.v;
    }
};

using u8 = std::uint8_t;   ///< Short name for uint8_t.
using u16 = std::uint16_t; ///< Short name for uint16_t.
using u32 = std::uint32_t; ///< Short name for uint32_t.
using u64 = std::uint64_t; ///< Short name for uint64_t.
using i8 = std::int8_t;    ///< Short name for int8_t.
using i16 = std::int16_t;  ///< Short name for int16_t.
using i32 = std::int32_t;  ///< Short name for int32_t.
using i64 = std::int64_t;  ///< Short name for int64_t.

using u8np = npint<u8>;   ///< Non-promoted unsigned 8-bit integer.
using u16np = npint<u16>; ///< Non-promoted unsigned 16-bit integer.
using u32np = npint<u32>; ///< Non-promoted unsigned 32-bit integer.
using u64np = npint<u64>; ///< Non-promoted unsigned 64-bit integer.
using i8np = npint<i8>;   ///< Non-promoted signed 8-bit integer.
using i16np = npint<i16>; ///< Non-promoted signed 16-bit integer.
using i32np = npint<i32>; ///< Non-promoted signed 32-bit integer.
using i64np = npint<i64>; ///< Non-promoted signed 64-bit integer.

} // namespace tcspc
