/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/timing_misc.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

TEST_CASE("retime periodic sequence events", "[retime_periodic_sequences]") {
    auto out = capture_output<event_set<periodic_sequence_event<>>>();
    auto in = feed_input<event_set<periodic_sequence_event<>>>(
        retime_periodic_sequences<>(10, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("normal operation") {
        in.feed(periodic_sequence_event<>{4, -8.0, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{-5, 1.0, 1.5}));
        in.feed(periodic_sequence_event<>{4, -8.5, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{-6, 1.5, 1.5}));
        in.feed(periodic_sequence_event<>{4, 10.0, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{13, 1.0, 1.5}));
    }

    SECTION("max time shift") {
        in.feed(periodic_sequence_event<>{4, -9.0, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{-6, 1.0, 1.5}));
        in.feed(periodic_sequence_event<>{4, 11.75, 1.5});
        REQUIRE(out.check(periodic_sequence_event<>{14, 1.75, 1.5}));
    }

    SECTION("fail above max time shift") {
        REQUIRE_THROWS_WITH(in.feed(periodic_sequence_event<>{4, -9.01, 1.5}),
                            Catch::Matchers::ContainsSubstring("shift"));
        REQUIRE_THROWS_WITH(in.feed(periodic_sequence_event<>{4, 12.0, 1.5}),
                            Catch::Matchers::ContainsSubstring("shift"));
    }
}

TEST_CASE("retime periodic sequence events unsigned",
          "[retime_periodic_sequences]") {
    struct traits {
        using abstime_type = std::uint64_t;
    };
    auto out = capture_output<event_set<periodic_sequence_event<traits>>>();
    auto in = feed_input<event_set<periodic_sequence_event<traits>>>(
        retime_periodic_sequences<traits>(10, ref_processor(out)));
    in.require_output_checked(out);

    SECTION("normal operation") {
        in.feed(periodic_sequence_event<traits>{4, -1.5, 1.5});
        REQUIRE(out.check(periodic_sequence_event<traits>{1, 1.5, 1.5}));
        in.feed(periodic_sequence_event<traits>{4, -3.0, 1.5});
        REQUIRE(out.check(periodic_sequence_event<traits>{0, 1.0, 1.5}));
    }

    SECTION("unsigned underflow") {
        REQUIRE_THROWS_WITH(
            in.feed(periodic_sequence_event<traits>{4, -3.01, 1.5}),
            Catch::Matchers::ContainsSubstring("unsigned"));
    }
}

} // namespace tcspc
