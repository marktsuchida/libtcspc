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

TEST_CASE("move to shared", "[move_to_shared]") {
    std::shared_ptr<int> const spi = move_to_shared(123);
    std::string s = "hello";
    // auto const sps = move_to_shared(s); // Fails to compile with lvalue.
    auto const sps = move_to_shared(std::move(s));
}

TEST_CASE("shared_processor", "[shared_processor]") {
    using e = empty_test_event<0>;
    auto out1 = capture_output<event_set<e>>();
    // Use ref_processor to get a movable downstream (capture_output is not).
    auto shref = move_to_shared(ref_processor(out1));

    auto in = feed_input<event_set<e>>(shared_processor(shref));

    SECTION("Events are forwarded") {
        in.require_output_checked(out1);
        in.feed(e{});
        REQUIRE(out1.check(e{}));
        in.flush();
        REQUIRE(out1.check_flushed());
    }

    SECTION("Downstream can be replaced via the shared_ptr") {
        auto out2 = capture_output<event_set<e>>();
        *shref = ref_processor(out2);
        in.require_output_checked(out2);
        in.flush();
        REQUIRE(out2.check_flushed());
    }
}

} // namespace tcspc
