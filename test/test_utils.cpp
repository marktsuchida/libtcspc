/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/test_utils.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"

#include <exception>
#include <ostream>
#include <stdexcept>

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = empty_test_event<0>;
using e1 = timestamped_test_event<1>;

TEST_CASE("Short-circuited with no events", "[test_utils]") {
    auto out = capture_output<event_set<>>();
    auto in = feed_input<event_set<>>(ref_processor(out));
    in.require_output_checked(out);

    SECTION("End successfully") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("End with error") {
        in.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
    }
}

TEST_CASE("Short-circuited with event set", "[test_utils]") {
    auto out = internal::capture_output<event_set<e0, e1>>(true);
    auto in = feed_input<event_set<e0, e1>>(ref_processor(out));
    in.require_output_checked(out);

    in.feed(e0{});
    REQUIRE(out.check(e0{}));

    in.feed(e1{42});
    REQUIRE(out.check(e1{42}));

    SECTION("End successfully") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("End with error") {
        in.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE_THROWS_AS(out.check_end(), std::runtime_error);
    }

    SECTION("Forget to check output before feeding event") {
        in.feed(e0{});
        REQUIRE_THROWS_AS(in.feed(e0{}), std::logic_error);
    }

    SECTION("Forget to check output before feeding end") {
        in.feed(e0{});
        REQUIRE_THROWS_AS(in.feed_end(), std::logic_error);
    }

    SECTION("Forget to check output before feeding end with error") {
        in.feed(e0{});
        REQUIRE_THROWS_AS(
            in.feed_end(std::make_exception_ptr(std::runtime_error("test"))),
            std::logic_error);
    }

    SECTION("Expect the wrong event") {
        in.feed(e1{42});
        REQUIRE_FALSE(out.check(e1{0}));
    }
}

} // namespace tcspc
