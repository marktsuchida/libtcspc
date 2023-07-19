/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/npint.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

static_assert(std::is_trivial_v<u8np>);

static_assert(sizeof(u8np) == sizeof(std::uint8_t));

static_assert(std::numeric_limits<i8np>::min() == i8np(-128));
static_assert(std::numeric_limits<i8np>::digits == 7);

TEST_CASE("integer construct and convert", "[integers]") {
    static constexpr u8np ue{};
    CHECK(std::uint8_t(ue) == 0);
    CHECK(ue.value() == 0);
    CHECK(u8np(3).value() == 3);
    CHECK(3_u8np == u8np(3));
}

TEST_CASE("integer conversions", "[integers]") {
    CHECK(u8np(3_i8np) == 3_u8np);
    CHECK(i8np(3_u8np) == 3_i8np);

    CHECK(u16np(3_u8np) == 3_u16np);
    CHECK(u16np(u8np(3_i8np)) == 3_u16np);
    CHECK(u16np(i16np(3_i8np)) == 3_u16np);

    CHECK(u8np(3_u16np) == 3_u8np);
    CHECK(i8np(3_u16np) == 3_i8np);
}

TEST_CASE("integer increment and decrement", "[integers]") {
    u8np ue{};
    CHECK((++ue).value() == 1);
    CHECK((ue++).value() == 1);
    CHECK(ue.value() == 2);
    CHECK((--ue).value() == 1);
    CHECK((ue--).value() == 1);
    CHECK(ue.value() == 0);
}

TEST_CASE("integer compound assignment operators", "[integers]") {
    u8np ue{};

    ue += 3_u8np;
    CHECK(ue.value() == 3);
    ue *= 6_u8np;
    CHECK(ue.value() == 18);
    ue -= 3_u8np;
    CHECK(ue.value() == 15);
    ue /= 6_u8np;
    CHECK(ue.value() == 2);
    ue %= 5_u8np;
    CHECK(ue.value() == 2);

    ue &= 2_u8np;
    CHECK(ue.value() == 2);
    ue |= 4_u8np;
    CHECK(ue.value() == 6);
    ue ^= 255_u8np;
    CHECK(ue.value() == 255 - 6);

    ue = 8_u8np;

    SECTION("shift with rhs of same type") {
        ue >>= 1_u8np;
        CHECK(ue.value() == 4);
        ue <<= 2_u8np;
        CHECK(ue.value() == 16);
    }

    SECTION("shift with rhs of other npint") {
        ue >>= 1_i16np;
        CHECK(ue.value() == 4);
        ue <<= 2_i16np;
        CHECK(ue.value() == 16);
    }

    SECTION("shift with rhs of raw integer type") {
        ue >>= 1;
        CHECK(ue.value() == 4);
        ue <<= 2;
        CHECK(ue.value() == 16);
    }
}

TEST_CASE("integer unary operators", "[integers]") {
    CHECK(+3_u8np == 3_u8np);
    CHECK(-3_u8np == 253_u8np);
    CHECK(~1_u8np == 254_u8np);
}

TEST_CASE("integer binary operators", "[integers]") {
    CHECK(3_u8np + 5_u8np == 8_u8np);
    CHECK(5_u8np - 3_u8np == 2_u8np);
    CHECK(3_u8np * 5_u8np == 15_u8np);
    CHECK(5_u8np / 3_u8np == 1_u8np);
    CHECK(5_u8np % 3_u8np == 2_u8np);
    CHECK((3_u8np & 2_u8np) == 2_u8np);
    CHECK((3_u8np | 4_u8np) == 7_u8np);
    CHECK((1_u8np ^ 255_u8np) == 254_u8np);

    SECTION("shift with rhs of same type") {
        CHECK((8_u8np >> 1_u8np) == 4_u8np);
        CHECK((4_u8np << 2_u8np) == 16_u8np);
    }

    SECTION("shift with rhs of other npint") {
        CHECK((8_u8np >> 1_u16np) == 4_u8np);
        CHECK((4_u8np << 2_u16np) == 16_u8np);
    }

    SECTION("shift with rhs of raw integer type") {
        CHECK((8_u8np >> 1) == 4_u8np);
        CHECK((4_u8np << 2) == 16_u8np);
    }
}

TEST_CASE("integer comparison operators", "[integers]") {
    CHECK(1_u8np == 1_u8np);
    CHECK_FALSE(1_u8np == 0_u8np);
    CHECK_FALSE(1_u8np != 1_u8np);
    CHECK(1_u8np != 0_u8np);
    CHECK_FALSE(1_u8np > 1_u8np);
    CHECK(1_u8np > 0_u8np);
    CHECK_FALSE(1_u8np < 1_u8np);
    CHECK(0_u8np < 1_u8np);
    CHECK(1_u8np >= 1_u8np);
    CHECK_FALSE(0_u8np >= 1_u8np);
    CHECK(1_u8np <= 1_u8np);
    CHECK_FALSE(1_u8np <= 0_u8np);
}

TEST_CASE("integer subclasses", "[integers]") {
    // Demonstrate how to use subclasses for strong typing of quantities.

    struct myu8 : u8np {
        // User can decide which constructors to allow, for example:

        // Needed to inherit all converting constructors:
        using u8np::u8np; // Does not include conversion from plain u8np.

        // Needed to allow conversion from plain u8np:
        explicit myu8(u8np v) noexcept : myu8(v.value()) {}
    };
    static constexpr myu8 ue{};
    static constexpr myu8 ue2(3);
    // Member and friend operators work:
    CHECK((ue + -ue2).value() == 256 - 3);

    // Convertible to plain type:
    CHECK(u8np(myu8(4)) == 4_u8np);

    // Convertible from plain type:
    myu8(0_u8np);
    myu8(0_u16np);
}

} // namespace tcspc
