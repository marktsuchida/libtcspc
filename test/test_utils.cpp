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
        in.flush();
        CHECK(out.check_flushed());
    }

    SECTION("Unflushed end") { CHECK(out.check_not_flushed()); }
}

TEST_CASE("Short-circuited with event set", "[test_utils]") {
    auto out = internal::capture_output<event_set<e0, e1>>(true);
    auto in = feed_input<event_set<e0, e1>>(ref_processor(out));
    in.require_output_checked(out);

    in.feed(e0{});
    CHECK(out.check(e0{}));

    in.feed(e1{42});
    CHECK(out.check(e1{42}));

    SECTION("End successfully") {
        in.flush();
        CHECK(out.check_flushed());
    }

    SECTION("Unflushed end") { CHECK(out.check_not_flushed()); }

    SECTION("Forget to check output before feeding event") {
        in.feed(e0{});
        CHECK_THROWS_AS(in.feed(e0{}), std::logic_error);
    }

    SECTION("Forget to check output before flushing") {
        in.feed(e0{});
        CHECK_THROWS_AS(in.flush(), std::logic_error);
    }

    SECTION("Forget to check output before asserting successful end") {
        in.feed(e0{});
        CHECK_FALSE(out.check_flushed());
    }

    SECTION("Forget to check output before asserting unflushed end") {
        in.feed(e0{});
        CHECK_FALSE(out.check_not_flushed());
    }

    SECTION("Expect the wrong event") {
        in.feed(e1{42});
        CHECK_FALSE(out.check(e1{0}));
    }
}

} // namespace tcspc
