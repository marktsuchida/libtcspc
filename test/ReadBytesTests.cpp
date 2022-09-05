/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/ReadBytes.hpp"

#include <catch2/catch.hpp>

using namespace flimevt::internal;

TEST_CASE("Read u16", "[ReadBytes]") {
    bool useMemcpy = GENERATE(false, true);
    auto f = useMemcpy ? ReadU16LE_Memcpy : ReadU16LE_Generic;

    unsigned char data[2];

    SECTION("Zero") {
        data[0] = data[1] = 0;
        REQUIRE(f(data) == 0U);
    }

    SECTION("Low byte") {
        unsigned x = GENERATE(0x01, 0x7f, 0x80, 0xff);
        data[0] = x;
        data[1] = 0;
        REQUIRE(f(data) == x);
    }

    SECTION("High byte") {
        unsigned x = GENERATE(0x01, 0x7f, 0x80, 0xff);
        data[0] = 0;
        data[1] = x;
        REQUIRE(f(data) == x * 0x100U);
    }
}

TEST_CASE("Read u32", "[ReadBytes]") {
    bool useMemcpy = GENERATE(false, true);
    auto f = useMemcpy ? ReadU32LE_Memcpy : ReadU32LE_Generic;

    unsigned char data[4];
    memset(data, 0, 4);
    int byte = GENERATE(0, 1, 2, 3);

    SECTION("Zero") { REQUIRE(f(data) == 0U); }

    SECTION("Single byte") {
        unsigned x = GENERATE(0x01, 0x7f, 0x80, 0xff);
        data[byte] = x;
        REQUIRE(f(data) == x * (1 << (8 * byte)));
    }

    SECTION("Sanity") {
        data[0] = 1;
        data[1] = 2;
        data[2] = 3;
        data[3] = 4;
        REQUIRE(f(data) == 0x04030201U);
    }
}

TEST_CASE("Read u64", "[ReadBytes]") {
    bool useMemcpy = GENERATE(false, true);
    auto f = useMemcpy ? ReadU64LE_Memcpy : ReadU64LE_Generic;

    unsigned char data[8];
    memset(data, 0, 8);
    int byte = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);

    SECTION("Zero") { REQUIRE(f(data) == 0U); }

    SECTION("Single byte") {
        unsigned x = GENERATE(0x01, 0x7f, 0x80, 0xff);
        data[byte] = x;
        REQUIRE(f(data) == x * (1ULL << (8 * byte)));
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
        REQUIRE(f(data) == 0x0807060504030201ULL);
    }
}
