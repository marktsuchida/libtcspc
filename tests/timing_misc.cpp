/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/timing_misc.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <memory>

namespace tcspc {

TEST_CASE("introspect timing_misc", "[introspect]") {
    check_introspect_simple_processor(
        retime_periodic_sequences(1, null_sink()));
    check_introspect_simple_processor(
        extrapolate_periodic_sequences(1, null_sink()));
    check_introspect_simple_processor(
        add_count_to_periodic_sequences(1, null_sink()));
    using etick = time_tagged_test_event<0>;
    using estart = time_tagged_test_event<1>;
    using estop = time_tagged_test_event<2>;
    check_introspect_simple_processor(
        convert_sequences_to_start_stop<etick, estart, estop>(1, null_sink()));
}

TEST_CASE("retime periodic sequence events") {
    using out_events = type_list<periodic_sequence_event<>>;
    auto ctx = processor_context::create();
    auto in = feed_input<type_list<periodic_sequence_event<>>>(
        retime_periodic_sequences<>(
            10, capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

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
    using out_events = type_list<periodic_sequence_event<traits>>;
    auto ctx = processor_context::create();
    auto in = feed_input<type_list<periodic_sequence_event<traits>>>(
        retime_periodic_sequences<traits>(
            10, capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

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
    using out_events = type_list<real_one_shot_timing_event<>>;
    auto ctx = processor_context::create();
    auto in = feed_input<type_list<periodic_sequence_event<>>>(
        extrapolate_periodic_sequences(
            2, capture_output<out_events>(
                   ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(periodic_sequence_event<>{42, 0.5, 1.75});
    REQUIRE(out.check(real_one_shot_timing_event<>{42, 4.0}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("add count to periodic sequences",
          "[add_count_to_periodic_sequences]") {
    using out_events = type_list<real_linear_timing_event<>>;
    auto ctx = processor_context::create();
    auto in = feed_input<type_list<periodic_sequence_event<>>>(
        add_count_to_periodic_sequences(
            3, capture_output<out_events>(
                   ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(periodic_sequence_event<>{42, 0.5, 1.75});
    REQUIRE(out.check(real_linear_timing_event<>{42, 0.5, 1.75, 3}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("convert sequences to start-stop",
          "[convert_sequences_to_start_stop]") {
    using inevt = time_tagged_test_event<0>;
    using startevt = time_tagged_test_event<1>;
    using stopevt = time_tagged_test_event<2>;
    using otherevt = time_tagged_test_event<3>;
    using out_events = type_list<startevt, stopevt, otherevt>;
    auto ctx = processor_context::create();

    SECTION("zero length") {
        auto in = feed_input<type_list<inevt, otherevt>>(
            convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                0, capture_output<out_events>(
                       ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(inevt{42}); // No output.
        in.feed(inevt{42}); // No output.
        in.feed(otherevt{43});
        REQUIRE(out.check(otherevt{43}));
        in.feed(inevt{42}); // No output.
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("length 1") {
        auto in = feed_input<type_list<inevt, otherevt>>(
            convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                1, capture_output<out_events>(
                       ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

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
        auto in = feed_input<type_list<inevt, otherevt>>(
            convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                2, capture_output<out_events>(
                       ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

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
