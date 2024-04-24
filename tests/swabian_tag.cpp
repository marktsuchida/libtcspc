/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/swabian_tag.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/npint.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <type_traits>

namespace tcspc {

static_assert(std::is_pod_v<swabian_tag_event>);

static_assert(sizeof(swabian_tag_event) == 16);

TEST_CASE("swabian tag equality and inequality") {
    auto const ptrn =
        std::array<u8, 16>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    swabian_tag_event e0{};
    swabian_tag_event e1{};
    CHECK(e0 == e1);
    std::memcpy(&e0, ptrn.data(), sizeof(e0));
    CHECK(e0 != e1);
    std::memcpy(&e1, ptrn.data(), sizeof(e1));
    CHECK(e0 == e1);

    auto const nonzero1 =
        std::array<u8, 16>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto const nonzero2 =
        std::array<u8, 16>{128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    swabian_tag_event e2{};
    swabian_tag_event e3{};
    std::memcpy(&e3, nonzero1.data(), sizeof(e3));
    CHECK(e2 != e3);
    std::memcpy(&e3, nonzero2.data(), sizeof(e3));
    CHECK(e2 != e3);
}

TEST_CASE("swabian tag read") {
    swabian_tag_event event{};
    CHECK(event.type() == swabian_tag_event::tag_type::time_tag);
    CHECK(event.missed_event_count() == 0_u16np);
    CHECK(event.channel() == 0_i32np);
    CHECK(event.time() == 0_i64np);

    std::memcpy(&event,
                std::array<u8, 16>{0, 0xff, 0xff, 0xff, 1, 0, 0, 0, 2, 0, 0, 0,
                                   0, 0, 0, 0}
                    .data(),
                16);
    CHECK(event.type() == swabian_tag_event::tag_type::time_tag);
    CHECK(event.channel() == 1_i32np);
    CHECK(event.time() == 2_i64np);

    std::memcpy(&event,
                std::array<u8, 16>{0, 0xff, 0xff, 0xff, 0, 0, 0, 1, 0, 0, 0, 0,
                                   0, 0, 0, 2}
                    .data(),
                16);
    CHECK(event.type() == swabian_tag_event::tag_type::time_tag);
    CHECK(event.channel() == 1_i32np << 24);
    CHECK(event.time() == 2_i64np << 56);

    std::memcpy(
        &event,
        std::array<u8, 16>{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
            .data(),
        16);
    CHECK(event.type() == swabian_tag_event::tag_type::error);

    std::memcpy(
        &event,
        std::array<u8, 16>{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
            .data(),
        16);
    CHECK(event.type() == swabian_tag_event::tag_type::overflow_begin);

    std::memcpy(
        &event,
        std::array<u8, 16>{3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
            .data(),
        16);
    CHECK(event.type() == swabian_tag_event::tag_type::overflow_end);

    std::memcpy(
        &event,
        std::array<u8, 16>{4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
            .data(),
        16);
    CHECK(event.type() == swabian_tag_event::tag_type::missed_events);
    CHECK(event.missed_event_count() == 0_u16np);

    std::memcpy(
        &event,
        std::array<u8, 16>{4, 0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
            .data(),
        16);
    CHECK(event.type() == swabian_tag_event::tag_type::missed_events);
    CHECK(event.missed_event_count() == 513_u16np);
}

TEST_CASE("swabian tag assign") {
    auto event = swabian_tag_event::make_time_tag(100_i64np, 3_i32np);
    std::array<u8, 16> bytes{0, 0, 0, 0, 3, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0};
    auto byte_span = as_bytes(span(bytes));
    CHECK(
        std::equal(event.bytes.begin(), event.bytes.end(), byte_span.begin()));

    event = swabian_tag_event::make_error(100_i64np);
    bytes = {1, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0};
    CHECK(
        std::equal(event.bytes.begin(), event.bytes.end(), byte_span.begin()));

    event = swabian_tag_event::make_overflow_begin(100_i64np);
    bytes = {2, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0};
    CHECK(
        std::equal(event.bytes.begin(), event.bytes.end(), byte_span.begin()));

    event = swabian_tag_event::make_overflow_end(100_i64np);
    bytes = {3, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0};
    CHECK(
        std::equal(event.bytes.begin(), event.bytes.end(), byte_span.begin()));

    event = swabian_tag_event::make_missed_events(100_i64np, 3_i32np, 7_u16np);
    bytes = {4, 0, 7, 0, 3, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0};
    CHECK(
        std::equal(event.bytes.begin(), event.bytes.end(), byte_span.begin()));
}

TEST_CASE("introspect swabian_tag", "[introspect]") {
    check_introspect_simple_processor(decode_swabian_tags(null_sink()));
}

TEST_CASE("decode swabian tags") {
    using out_events =
        type_list<detection_event<>, begin_lost_interval_event<>,
                  end_lost_interval_event<>, lost_counts_event<>,
                  warning_event>;
    auto ctx = context::create();
    auto in = feed_input<type_list<swabian_tag_event>>(
        decode_swabian_tags(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    SECTION("time tag") {
        in.feed(swabian_tag_event::make_time_tag(42_i64np, 5_i32np));
        REQUIRE(out.check(detection_event<>{42, 5}));
    }

    SECTION("error") {
        in.feed(swabian_tag_event::make_error(42_i64np));
        auto const e = out.pop<warning_event>();
        REQUIRE_THAT(e.message, Catch::Matchers::ContainsSubstring("error"));
    }

    SECTION("overflow begin") {
        in.feed(swabian_tag_event::make_overflow_begin(42_i64np));
        REQUIRE(out.check(begin_lost_interval_event<>{42}));
    }

    SECTION("overflow end") {
        in.feed(swabian_tag_event::make_overflow_end(42_i64np));
        REQUIRE(out.check(end_lost_interval_event<>{42}));
    }

    SECTION("missed events") {
        in.feed(swabian_tag_event::make_missed_events(42_i64np, 5_i32np,
                                                      123_u16np));
        REQUIRE(out.check(lost_counts_event<>{42, 5, 123}));
    }

    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
