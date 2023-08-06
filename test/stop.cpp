/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/stop.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

TEST_CASE("stop with error", "[stop_with_error]") {
    auto out = capture_output<event_set<e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        stop_with_error<event_set<e0>>("myerror", ref_processor(out)));
    in.require_output_checked(out);

    in.feed(e1{});
    REQUIRE(out.check(e1{}));
    REQUIRE_THROWS_WITH(
        in.feed(e0{}),
        Catch::Matchers::ContainsSubstring("myerror") &&
            Catch::Matchers::ContainsSubstring("empty_test_event<0>"));
    REQUIRE(out.check_not_flushed());
}

TEST_CASE("stop with no error", "[stop]") {
    auto out = capture_output<event_set<e1>>();
    auto in =
        feed_input<event_set<e0, e1>>(stop<event_set<e0>>(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(e1{});
    REQUIRE(out.check(e1{}));
    REQUIRE_THROWS_AS(in.feed(e0{}), end_processing);
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
