/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/check.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

TEST_CASE("check monotonic", "[check_monotonic]") {
    using e0 = timestamped_test_event<0>;
    auto out = capture_output<event_set<e0, warning_event>>();
    auto in = feed_input<event_set<e0, warning_event>>(
        check_monotonic(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(e0{-10});
    REQUIRE(out.check(e0{-10}));
    in.feed(warning_event{"test"});
    REQUIRE(out.check(warning_event{"test"}));
    in.feed(e0{-10});
    REQUIRE(out.check(e0{-10}));
    in.feed(e0{-11});
    auto const out_event = out.retrieve<warning_event>();
    REQUIRE(out_event.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    CHECK(out_event->message.find("monotonic") != std::string::npos);
    REQUIRE(out.check(e0{-11}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("check alternating", "[check_alternating]") {
    using e0 = timestamped_test_event<0>;
    using e1 = timestamped_test_event<1>;
    using e2 = timestamped_test_event<2>;
    auto out = capture_output<event_set<e0, e1, e2, warning_event>>();
    auto in = feed_input<event_set<e0, e1, e2>>(
        check_alternating<e0, e1>(ref_processor(out)));
    in.require_output_checked(out);

    SECTION("correct") {
        in.feed(e0{42});
        REQUIRE(out.check(e0{42}));
        in.feed(e1{43});
        REQUIRE(out.check(e1{43}));
        in.feed(e0{44});
        REQUIRE(out.check(e0{44}));
        in.feed(e1{45});
        REQUIRE(out.check(e1{45}));
        in.feed(e0{46});
        REQUIRE(out.check(e0{46}));
        in.feed(e2{47});
        REQUIRE(out.check(e2{47}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("wrong event first") {
        in.feed(e1{42});
        auto const out_event = out.retrieve<warning_event>();
        REQUIRE(out_event.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        CHECK(out_event->message.find("alternat") != std::string::npos);
        REQUIRE(out.check(e1{42}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("consecutive Event0") {
        in.feed(e0{42});
        REQUIRE(out.check(e0{42}));
        in.feed(e0{43});
        auto const out_event = out.retrieve<warning_event>();
        REQUIRE(out_event.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        CHECK(out_event->message.find("alternat") != std::string::npos);
        REQUIRE(out.check(e0{43}));
    }

    SECTION("consecutive Event1") {
        in.feed(e0{42});
        REQUIRE(out.check(e0{42}));
        in.feed(e1{43});
        REQUIRE(out.check(e1{43}));
        in.feed(e1{44});
        auto const out_event = out.retrieve<warning_event>();
        REQUIRE(out_event.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        CHECK(out_event->message.find("alternat") != std::string::npos);
        REQUIRE(out.check(e1{44}));
    }
}

} // namespace tcspc
