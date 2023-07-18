/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/delay.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = timestamped_test_event<0>;
using e1 = timestamped_test_event<1>;

TEST_CASE("Delay", "[delay]") {
    auto out = capture_output<event_set<e0, e1>>();

    SECTION("Zero delay is noop") {
        auto in = feed_input<event_set<e0>>(
            delay<default_data_traits>(0, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(e0{0});
        REQUIRE(out.check(e0{0}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Delay +1") {
        auto in = feed_input<event_set<e0, e1>>(
            delay<default_data_traits>(1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(e0{0});
        REQUIRE(out.check(e0{1}));
        in.feed(e1{1});
        REQUIRE(out.check(e1{2}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Delay -1") {
        auto in = feed_input<event_set<e0, e1>>(
            delay<default_data_traits>(-1, ref_processor(out)));
        in.require_output_checked(out);

        in.feed(e0{0});
        REQUIRE(out.check(e0{-1}));
        in.feed(e1{1});
        REQUIRE(out.check(e1{0}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("zero-base abstime", "[zero_base_abstime]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in =
        feed_input<event_set<e0, e1>>(zero_base_abstime(ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Positive") {
        in.feed(e0{123});
        REQUIRE(out.check(e0{0}));
        in.feed(e1{125});
        REQUIRE(out.check(e1{2}));
        in.feed(
            e0{std::numeric_limits<default_data_traits::abstime_type>::min()});
        REQUIRE(out.check(
            e0{std::numeric_limits<default_data_traits::abstime_type>::max() -
               122}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Negative") {
        in.feed(e0{-123});
        REQUIRE(out.check(e0{0}));
        in.feed(e1{-121});
        REQUIRE(out.check(e1{2}));
        in.feed(
            e0{std::numeric_limits<default_data_traits::abstime_type>::max()});
        REQUIRE(out.check(
            e0{std::numeric_limits<default_data_traits::abstime_type>::min() +
               122}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

} // namespace tcspc
