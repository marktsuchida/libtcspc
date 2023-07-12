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
#include <limits>
#include <ostream>
#include <type_traits>

namespace tcspc {

/**
 * \brief Non-promoted integers.
 *
 * \ingroup misc
 *
 * Objects of this type behave similarly to the underlying integer type, except
 * that no integer promotion is applied automatically and no implicit
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

/**
 * \brief Short name for uint8_t.
 *
 * \ingroup misc
 */
using u8 = std::uint8_t;

/**
 * \brief Short name for uint16_t.

 * \ingroup misc
 */
using u16 = std::uint16_t;

/**
 * \brief Short name for uint32_t.
 *
 * \ingroup misc
 */
using u32 = std::uint32_t;

/**
 * \brief Short name for uint64_t.
 *
 * \ingroup misc
 */
using u64 = std::uint64_t;

/**
 * \brief Short name for int8_t.

 * \ingroup misc
 */
using i8 = std::int8_t;

/**
 * \brief Short name for int16_t.

 * \ingroup misc
 */
using i16 = std::int16_t;

/**
 * \brief Short name for int32_t.

 * \ingroup misc
 */
using i32 = std::int32_t;

/**
 * \brief Short name for int64_t.
 *
 * \ingroup misc
 */
using i64 = std::int64_t;

/**
 * \brief Non-promoted unsigned 8-bit integer.
 *
 * \ingroup misc
 */
using u8np = npint<u8>;

/**
 * \brief Non-promoted unsigned 16-bit integer.
 *
 * \ingroup misc
 */
using u16np = npint<u16>;

/**
 * \brief Non-promoted unsigned 32-bit integer.

 * \ingroup misc
 */
using u32np = npint<u32>;

/**
 * \brief Non-promoted unsigned 64-bit integer.

 * \ingroup misc
 */
using u64np = npint<u64>;

/**
 * \brief Non-promoted signed 8-bit integer.

 * \ingroup misc
 */
using i8np = npint<i8>;

/**
 * \brief Non-promoted signed 16-bit integer.

 * \ingroup misc
 */
using i16np = npint<i16>;

/**
 * \brief Non-promoted signed 32-bit integer.

 * \ingroup misc
 */
using i32np = npint<i32>;

/**
 * \brief Non-promoted signed 64-bit integer.

 * \ingroup misc
 */
using i64np = npint<i64>;

/**
 * \brief User-defined literals for \ref npint.
 */
namespace literals {

/**
 * \brief User defined literal for \ref u8np.
 *
 * \ingroup misc
 */
constexpr auto operator""_u8np(unsigned long long v) -> u8np {
    return u8np(v);
}

/**
 * \brief User defined literal for \ref u16np.
 *
 * \ingroup misc
 */
constexpr auto operator""_u16np(unsigned long long v) -> u16np {
    return u16np(v);
}

/**
 * \brief User defined literal for \ref u32np.
 *
 * \ingroup misc
 */
constexpr auto operator""_u32np(unsigned long long v) -> u32np {
    return u32np(v);
}

/**
 * \brief User defined literal for \ref u64np.
 *
 * \ingroup misc
 */
constexpr auto operator""_u64np(unsigned long long v) -> u64np {
    return u64np(v);
}

/**
 * \brief User defined literal for \ref i8np.
 *
 * \ingroup misc
 */
constexpr auto operator""_i8np(unsigned long long v) -> i8np {
    return i8np(v);
}

/**
 * \brief User defined literal for \ref i16np.
 *
 * \ingroup misc
 */
constexpr auto operator""_i16np(unsigned long long v) -> i16np {
    return i16np(v);
}

/**
 * \brief User defined literal for \ref i32np.
 *
 * \ingroup misc
 */
constexpr auto operator""_i32np(unsigned long long v) -> i32np {
    return i32np(v);
}

/**
 * \brief User defined literal for \ref i64np.
 *
 * \ingroup misc
 */
constexpr auto operator""_i64np(unsigned long long v) -> i64np {
    return i64np(v);
}

} // namespace literals

using namespace literals;

} // namespace tcspc

namespace std {

template <typename T> struct numeric_limits<tcspc::npint<T>> {
    static constexpr bool is_specialized = true;

    static constexpr bool has_denorm_loss = numeric_limits<T>::has_denorm_loss;
    static constexpr bool has_infinity = numeric_limits<T>::has_infinity;
    static constexpr bool has_quiet_NaN = numeric_limits<T>::has_quiet_NaN;
    static constexpr bool has_signaling_NaN =
        numeric_limits<T>::has_signaling_NaN;
    static constexpr bool is_bounded = numeric_limits<T>::is_bounded;
    static constexpr bool is_exact = numeric_limits<T>::is_exact;
    static constexpr bool is_iec559 = numeric_limits<T>::is_iec559;
    static constexpr bool is_integer = numeric_limits<T>::is_integer;
    static constexpr bool is_modulo = numeric_limits<T>::is_modulo;
    static constexpr bool is_signed = numeric_limits<T>::is_signed;
    static constexpr bool tinyness_before = numeric_limits<T>::tinyness_before;
    static constexpr bool traps = numeric_limits<T>::traps;
    static constexpr float_denorm_style has_denorm =
        numeric_limits<T>::has_denorm;
    static constexpr float_round_style round_style =
        numeric_limits<T>::round_style;
    static constexpr int digits = numeric_limits<T>::digits;
    static constexpr int digits10 = numeric_limits<T>::digits10;
    static constexpr int max_digits10 = numeric_limits<T>::max_digits10;
    static constexpr int max_exponent = numeric_limits<T>::max_exponent;
    static constexpr int max_exponent10 = numeric_limits<T>::max_exponent10;
    static constexpr int min_exponent = numeric_limits<T>::min_exponent;
    static constexpr int min_exponent10 = numeric_limits<T>::min_exponent10;
    static constexpr int radix = numeric_limits<T>::radix;

    static constexpr tcspc::npint<T> denorm_min() {
        return tcspc::npint<T>(numeric_limits<T>::denorm_min());
    }
    static constexpr tcspc::npint<T> epsilon() {
        return tcspc::npint<T>(numeric_limits<T>::epsilon());
    }
    static constexpr tcspc::npint<T> infinity() {
        return tcspc::npint<T>(numeric_limits<T>::infinity());
    }
    static constexpr tcspc::npint<T> lowest() {
        return tcspc::npint<T>(numeric_limits<T>::lowest());
    }
    static constexpr tcspc::npint<T> max() {
        return tcspc::npint<T>(numeric_limits<T>::max());
    }
    static constexpr tcspc::npint<T> min() {
        return tcspc::npint<T>(numeric_limits<T>::min());
    }
    static constexpr tcspc::npint<T> quiet_NaN() {
        return tcspc::npint<T>(numeric_limits<T>::quiet_NaN());
    }
    static constexpr tcspc::npint<T> round_error() {
        return tcspc::npint<T>(numeric_limits<T>::round_error());
    }
    static constexpr tcspc::npint<T> signaling_NaN() {
        return tcspc::npint<T>(numeric_limits<T>::signaling_NaN());
    }
};

} // namespace std
