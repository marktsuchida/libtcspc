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

TEST_CASE("integer construct and convert", "[integers]") {
    u8np ue{};
    CHECK(std::uint8_t(ue) == 0);
    CHECK(ue.value() == 0);
    CHECK(u8np(3).value() == 3);
}

TEST_CASE("integer conversions", "[integers]") {
    CHECK(u8np(i8np(3)) == u8np(3));
    CHECK(i8np(u8np(3)) == i8np(3));

    CHECK(u16np(u8np(3)) == u16np(3));
    CHECK(u16np(u8np(i8np(3))) == u16np(3));
    CHECK(u16np(i16np(i8np(3))) == u16np(3));

    CHECK(u8np(u16np(3)) == u8np(3));
    CHECK(i8np(u16np(3)) == i8np(3));
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

    ue += u8np(3);
    CHECK(ue.value() == 3);
    ue *= u8np(6);
    CHECK(ue.value() == 18);
    ue -= u8np(3);
    CHECK(ue.value() == 15);
    ue /= u8np(6);
    CHECK(ue.value() == 2);
    ue %= u8np(5);
    CHECK(ue.value() == 2);

    ue &= u8np(2);
    CHECK(ue.value() == 2);
    ue |= u8np(4);
    CHECK(ue.value() == 6);
    ue ^= u8np(255);
    CHECK(ue.value() == 255 - 6);

    ue = u8np(8);

    SECTION("shift with rhs of same type") {
        ue >>= u8np(1);
        CHECK(ue.value() == 4);
        ue <<= u8np(2);
        CHECK(ue.value() == 16);
    }

    SECTION("shift with rhs of other npint") {
        ue >>= i16np(1);
        CHECK(ue.value() == 4);
        ue <<= i16np(2);
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
    CHECK(+u8np(3) == u8np(3));
    CHECK(-u8np(3) == u8np(253));
    CHECK(~u8np(1) == u8np(254));
}

TEST_CASE("integer binary operators", "[integers]") {
    CHECK(u8np(3) + u8np(5) == u8np(8));
    CHECK(u8np(5) - u8np(3) == u8np(2));
    CHECK(u8np(3) * u8np(5) == u8np(15));
    CHECK(u8np(5) / u8np(3) == u8np(1));
    CHECK(u8np(5) % u8np(3) == u8np(2));
    CHECK((u8np(3) & u8np(2)) == u8np(2));
    CHECK((u8np(3) | u8np(4)) == u8np(7));
    CHECK((u8np(1) ^ u8np(255)) == u8np(254));

    SECTION("shift with rhs of same type") {
        CHECK((u8np(8) >> u8np(1)) == u8np(4));
        CHECK((u8np(4) << u8np(2)) == u8np(16));
    }

    SECTION("shift with rhs of other npint") {
        CHECK((u8np(8) >> u16np(1)) == u8np(4));
        CHECK((u8np(4) << u16np(2)) == u8np(16));
    }

    SECTION("shift with rhs of raw integer type") {
        CHECK((u8np(8) >> 1) == u8np(4));
        CHECK((u8np(4) << 2) == u8np(16));
    }
}

TEST_CASE("integer comparison operators", "[integers]") {
    CHECK(u8np(1) == u8np(1));
    CHECK_FALSE(u8np(1) == u8np(0));
    CHECK_FALSE(u8np(1) != u8np(1));
    CHECK(u8np(1) != u8np(0));
    CHECK_FALSE(u8np(1) > u8np(1));
    CHECK(u8np(1) > u8np(0));
    CHECK_FALSE(u8np(1) < u8np(1));
    CHECK(u8np(0) < u8np(1));
    CHECK(u8np(1) >= u8np(1));
    CHECK_FALSE(u8np(0) >= u8np(1));
    CHECK(u8np(1) <= u8np(1));
    CHECK_FALSE(u8np(1) <= u8np(0));
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
    myu8 ue{};
    myu8 ue2(3);
    // Member and friend operators work:
    CHECK((ue + -ue2).value() == 256 - 3);

    // Convertible to plain type:
    CHECK(u8np(myu8(4)) == u8np(4));

    // Convertible from plain type:
    myu8(u8np(0));
    myu8(u16np(0));
}

} // namespace tcspc
