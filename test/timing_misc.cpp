/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/timing_misc.hpp"

#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

TEST_CASE("retime periodic sequence events") {
    using out_events = event_set<periodic_sequence_event<>>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<periodic_sequence_event<>>>(
        retime_periodic_sequences<>(
            10, capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

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
    using out_events = event_set<periodic_sequence_event<traits>>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<periodic_sequence_event<traits>>>(
        retime_periodic_sequences<traits>(
            10, capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

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

TEST_CASE("extrapolate periodic sequences",
          "[extrapolate_periodic_sequences]") {
    using out_events = event_set<real_one_shot_timing_event<>>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<periodic_sequence_event<>>>(
        extrapolate_periodic_sequences(
            2, capture_output<out_events>(
                   ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(periodic_sequence_event<>{42, 0.5, 1.75});
    REQUIRE(out.check(real_one_shot_timing_event<>{42, 4.0}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("add count to periodic sequences",
          "[add_count_to_periodic_sequences]") {
    using out_events = event_set<real_linear_timing_event<>>;
    auto ctx = std::make_shared<processor_context>();
    auto in = feed_input<event_set<periodic_sequence_event<>>>(
        add_count_to_periodic_sequences(
            3, capture_output<out_events>(
                   ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->accessor<capture_output_access>("out"));

    in.feed(periodic_sequence_event<>{42, 0.5, 1.75});
    REQUIRE(out.check(real_linear_timing_event<>{42, 0.5, 1.75, 3}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("convert sequences to start-stop",
          "[convert_sequences_to_start_stop]") {
    using inevt = timestamped_test_event<0>;
    using startevt = timestamped_test_event<1>;
    using stopevt = timestamped_test_event<2>;
    using otherevt = timestamped_test_event<3>;
    using out_events = event_set<startevt, stopevt, otherevt>;
    auto ctx = std::make_shared<processor_context>();

    SECTION("zero length") {
        auto in = feed_input<event_set<inevt, otherevt>>(
            convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                0, capture_output<out_events>(
                       ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(inevt{42}); // No output.
        in.feed(inevt{42}); // No output.
        in.feed(otherevt{43});
        REQUIRE(out.check(otherevt{43}));
        in.feed(inevt{42}); // No output.
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("length 1") {
        auto in = feed_input<event_set<inevt, otherevt>>(
            convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                1, capture_output<out_events>(
                       ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(inevt{42});
        REQUIRE(out.check(startevt{42}));
        in.feed(inevt{43});
        REQUIRE(out.check(stopevt{43}));
        in.feed(inevt{44});
        REQUIRE(out.check(startevt{44}));
        in.feed(inevt{45});
        REQUIRE(out.check(stopevt{45}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("length 2") {
        auto in = feed_input<event_set<inevt, otherevt>>(
            convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                2, capture_output<out_events>(
                       ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->accessor<capture_output_access>("out"));

        in.feed(inevt{42});
        REQUIRE(out.check(startevt{42}));
        in.feed(inevt{43});
        REQUIRE(out.check(stopevt{43}));
        REQUIRE(out.check(startevt{43}));
        in.feed(inevt{44});
        REQUIRE(out.check(stopevt{44}));

        in.feed(inevt{46});
        REQUIRE(out.check(startevt{46}));
        in.feed(inevt{47});
        REQUIRE(out.check(stopevt{47}));
        REQUIRE(out.check(startevt{47}));
        in.feed(inevt{48});
        REQUIRE(out.check(stopevt{48}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
