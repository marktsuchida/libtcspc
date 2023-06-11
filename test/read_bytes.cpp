/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/read_bytes.hpp"

#include <catch2/catch.hpp>

#include <array>

using namespace flimevt::internal;

TEST_CASE("Read u16", "[read_bytes]") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? read_u16le_memcpy : read_u16le_generic;

    std::array<unsigned char, 2> data{};

    SECTION("Zero") {
        data = {0, 0};
        REQUIRE(f(data.data()) == 0u);
    }

    SECTION("Low byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        data = {x, 0};
        REQUIRE(f(data.data()) == x);
    }

    SECTION("High byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        data = {0, x};
        REQUIRE(f(data.data()) == x * 0x100u);
    }
}

TEST_CASE("Read u32", "[read_bytes]") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? read_u32le_memcpy : read_u32le_generic;

    std::array<unsigned char, 4> data{};
    memset(data.data(), 0, 4);
    std::size_t const byte = GENERATE(0u, 1u, 2u, 3u);

    SECTION("Zero") { REQUIRE(f(data.data()) == 0u); }

    SECTION("Single byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        data[byte] = x;
        REQUIRE(f(data.data()) == x * (1 << (8 * byte)));
    }

    SECTION("Sanity") {
        data[0] = 1;
        data[1] = 2;
        data[2] = 3;
        data[3] = 4;
        REQUIRE(f(data.data()) == 0x04030201u);
    }
}

TEST_CASE("Read u64", "[read_bytes]") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? read_u64le_memcpy : read_u64le_generic;

    std::array<unsigned char, 8> data{};
    memset(data.data(), 0, 8);
    std::size_t const byte = GENERATE(0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u);

    SECTION("Zero") { REQUIRE(f(data.data()) == 0u); }

    SECTION("Single byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        data[byte] = x;
        REQUIRE(f(data.data()) == x * (1uLL << (8 * byte)));
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
        REQUIRE(f(data.data()) == 0x0807060504030201uLL);
    }
}
