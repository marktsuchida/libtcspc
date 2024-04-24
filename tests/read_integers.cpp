/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/read_integers.hpp"

#include "libtcspc/npint.hpp"
#include "libtcspc/span.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tcspc {

TEST_CASE("Read u8 compiles") {
    std::array<std::byte, 2> data{};
    CHECK(read_u8_at<0>(span(data)) == 0_u8np);
    CHECK(read_u8_at<1>(span(std::as_const(data))) == 0_u8np);
}

TEST_CASE("Read u16 compiles") {
    std::array<std::byte, 3> data{};
    CHECK(read_u16le_at<0>(span(data)) == 0_u16np);
    CHECK(read_u16le_at<1>(span(std::as_const(data))) == 0_u16np);
}

TEST_CASE("Read u16np impl") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? internal::read_u16le_memcpy
                        : internal::read_u16le_generic;

    std::array<unsigned char, 2> data{};

    SECTION("Zero") {
        data = {0, 0};
        REQUIRE(f(as_bytes(span(data))) == 0_u16np);
    }

    SECTION("Low byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        data = {x, 0};
        REQUIRE(f(as_bytes(span(data))) == u16np(x));
    }

    SECTION("High byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        data = {0, x};
        REQUIRE(f(as_bytes(span(data))) == u16np(x * 0x100u));
    }
}

TEST_CASE("Read u32np impl") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? internal::read_u32le_memcpy
                        : internal::read_u32le_generic;

    std::array<unsigned char, 4> data{};
    memset(data.data(), 0, 4);
    std::size_t const byte = GENERATE(0u, 1u, 2u, 3u);

    SECTION("Zero") { REQUIRE(f(as_bytes(span(data))) == 0_u32np); }

    SECTION("Single byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        data[byte] = x;
        REQUIRE(f(as_bytes(span(data))) == u32np(x * (1u << (8 * byte))));
    }

    SECTION("Sanity") {
        data[0] = 1;
        data[1] = 2;
        data[2] = 3;
        data[3] = 4;
        REQUIRE(f(as_bytes(span(data))) == 0x0403'0201_u32np);
    }
}

TEST_CASE("Read u64np impl") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? internal::read_u64le_memcpy
                        : internal::read_u64le_generic;

    std::array<unsigned char, 8> data{};
    memset(data.data(), 0, 8);
    std::size_t const byte = GENERATE(0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u);

    SECTION("Zero") { REQUIRE(f(as_bytes(span(data))) == 0_u64np); }

    SECTION("Single byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        data[byte] = x;
        REQUIRE(f(as_bytes(span(data))) == u64np(x * (1uLL << (8 * byte))));
    }

    SECTION("Sanity") {
        data[0] = 1;
        data[1] = 2;
        data[2] = 3;
        data[3] = 4;
        data[4] = 5;
        data[5] = 6;
        data[6] = 7;
        data[7] = 8;
        REQUIRE(f(as_bytes(span(data))) == 0x0807'0605'0403'0201_u64np);
    }
}

} // namespace tcspc
