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

} // namespace tcspc
