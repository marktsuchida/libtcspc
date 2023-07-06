/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/shared_processor.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

TEST_CASE("shared_processor", "[shared_processor]") {
    auto out1 = capture_output<event_set<>>();
    auto ref1 = ref_processor(out1);
    auto shptr = std::make_shared<decltype(ref1)>(std::move(ref1));

    auto in = feed_input<event_set<>>(shared_processor(shptr));

    SECTION("Events are forwarded") {
        in.require_output_checked(out1);
        in.feed_end();
        REQUIRE(out1.check_end());
    }

    SECTION("Downstream can be replaced via the shared_ptr") {
        auto out2 = capture_output<event_set<>>();
        *shptr = ref_processor(out2);
        in.require_output_checked(out2);
        in.feed_end();
        REQUIRE(out2.check_end());
    }
}

} // namespace tcspc
