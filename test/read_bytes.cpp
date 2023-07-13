/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/read_bytes.hpp"

#include <catch2/catch_all.hpp>

#include <array>

namespace tcspc {

namespace {

template <std::size_t N>
inline auto
byte_span_of_uchars(std::array<unsigned char, N> const &arr) noexcept
    -> byte_span<N> {
    void const *begin = &*arr.begin();
    void const *end = &*arr.end();
    return byte_span<N>(static_cast<std::byte const *>(begin),
                        static_cast<std::byte const *>(end));
}

} // namespace

TEST_CASE("Read u16np", "[read_bytes]") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? internal::read_u16le_memcpy
                        : internal::read_u16le_generic;

    std::array<unsigned char, 2> data{};

    SECTION("Zero") {
        data = {0, 0};
        REQUIRE(f(byte_span_of_uchars(data)) == 0_u16np);
    }

    SECTION("Low byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        data = {x, 0};
        REQUIRE(f(byte_span_of_uchars(data)) == u16np(x));
    }

    SECTION("High byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        data = {0, x};
        REQUIRE(f(byte_span_of_uchars(data)) == u16np(x * 0x100u));
    }
}

TEST_CASE("Read u32np", "[read_bytes]") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? internal::read_u32le_memcpy
                        : internal::read_u32le_generic;

    std::array<unsigned char, 4> data{};
    memset(data.data(), 0, 4);
    std::size_t const byte = GENERATE(0u, 1u, 2u, 3u);

    SECTION("Zero") { REQUIRE(f(byte_span_of_uchars(data)) == 0_u32np); }

    SECTION("Single byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        data[byte] = x;
        REQUIRE(f(byte_span_of_uchars(data)) == u32np(x * (1u << (8 * byte))));
    }

    SECTION("Sanity") {
        data[0] = 1;
        data[1] = 2;
        data[2] = 3;
        data[3] = 4;
        REQUIRE(f(byte_span_of_uchars(data)) == 0x0403'0201_u32np);
    }
}

TEST_CASE("Read u64np", "[read_bytes]") {
    bool const use_memcpy = GENERATE(false, true);
    auto f = use_memcpy ? internal::read_u64le_memcpy
                        : internal::read_u64le_generic;

    std::array<unsigned char, 8> data{};
    memset(data.data(), 0, 8);
    std::size_t const byte = GENERATE(0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u);

    SECTION("Zero") { REQUIRE(f(byte_span_of_uchars(data)) == 0_u64np); }

    SECTION("Single byte") {
        auto x = GENERATE(std::uint8_t(0x01), 0x7f, 0x80, 0xff);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        data[byte] = x;
        REQUIRE(f(byte_span_of_uchars(data)) ==
                u64np(x * (1uLL << (8 * byte))));
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
        REQUIRE(f(byte_span_of_uchars(data)) == 0x0807'0605'0403'0201_u64np);
    }
}

} // namespace tcspc
