/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/timing_misc.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace tcspc {

TEST_CASE("type constraints: retime_periodic_sequences") {
    using proc_type = decltype(retime_periodic_sequences(
        arg::max_time_shift<i64>{42},
        sink_events<periodic_sequence_model_event<>, int>()));
    STATIC_CHECK(is_processor_v<proc_type, periodic_sequence_model_event<>>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("type constraints: extrapolate_periodic_sequences") {
    using proc_type = decltype(extrapolate_periodic_sequences(
        arg::tick_index<std::size_t>{42},
        sink_events<real_one_shot_timing_event<>, int>()));
    STATIC_CHECK(
        is_processor_v<proc_type, periodic_sequence_model_event<>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("type constraints: add_count_to_periodic_sequences") {
    using proc_type = decltype(add_count_to_periodic_sequences(
        arg::count<std::size_t>{42},
        sink_events<real_linear_timing_event<>, int>()));
    STATIC_CHECK(
        is_processor_v<proc_type, periodic_sequence_model_event<>, int>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("type constraints: convert_sequences_to_start_stop") {
    struct tick {
        i64 abstime;
    };
    struct start {
        i64 abstime;
    };
    struct stop {
        i64 abstime;
    };
    using proc_type =
        decltype(convert_sequences_to_start_stop<tick, start, stop>(
            arg::count<std::size_t>{42}, sink_events<start, stop, int>()));
    STATIC_CHECK(is_processor_v<proc_type, tick, int>);
    STATIC_CHECK(handles_events_v<proc_type, start, stop>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, double>);
}

TEST_CASE("introspect: timing_misc") {
    check_introspect_simple_processor(
        retime_periodic_sequences(arg::max_time_shift<i64>{1}, null_sink()));
    check_introspect_simple_processor(extrapolate_periodic_sequences(
        arg::tick_index<std::size_t>{1}, null_sink()));
    check_introspect_simple_processor(add_count_to_periodic_sequences(
        arg::count<std::size_t>{1}, null_sink()));
    using etick = time_tagged_test_event<0>;
    using estart = time_tagged_test_event<1>;
    using estop = time_tagged_test_event<2>;
    check_introspect_simple_processor(
        convert_sequences_to_start_stop<etick, estart, estop>(
            arg::count<std::size_t>{1}, null_sink()));
}

TEST_CASE("retime periodic sequence events") {
    using out_events = type_list<periodic_sequence_model_event<>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         retime_periodic_sequences<>(
                             arg::max_time_shift<i64>{10},
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("normal operation") {
        in.handle(periodic_sequence_model_event<>{4, -8.0, 1.5});
        REQUIRE(out.check(periodic_sequence_model_event<>{-5, 1.0, 1.5}));
        in.handle(periodic_sequence_model_event<>{4, -8.5, 1.5});
        REQUIRE(out.check(periodic_sequence_model_event<>{-6, 1.5, 1.5}));
        in.handle(periodic_sequence_model_event<>{4, 10.0, 1.5});
        REQUIRE(out.check(periodic_sequence_model_event<>{13, 1.0, 1.5}));
    }

    SECTION("max time shift") {
        in.handle(periodic_sequence_model_event<>{4, -9.0, 1.5});
        REQUIRE(out.check(periodic_sequence_model_event<>{-6, 1.0, 1.5}));
        in.handle(periodic_sequence_model_event<>{4, 11.75, 1.5});
        REQUIRE(out.check(periodic_sequence_model_event<>{14, 1.75, 1.5}));
    }

    SECTION("fail above max time shift") {
        REQUIRE_THROWS_AS(
            in.handle(periodic_sequence_model_event<>{4, -9.01, 1.5}),
            data_validation_error);
        REQUIRE_THROWS_AS(
            in.handle(periodic_sequence_model_event<>{4, 12.0, 1.5}),
            data_validation_error);
    }
}

TEST_CASE("retime periodic sequence events unsigned",
          "[retime_periodic_sequences]") {
    struct types {
        using abstime_type = std::uint64_t;
    };
    using out_events = type_list<periodic_sequence_model_event<types>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         retime_periodic_sequences<types>(
                             arg::max_time_shift<u64>{10},
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("normal operation") {
        in.handle(periodic_sequence_model_event<types>{4, -1.5, 1.5});
        REQUIRE(out.check(periodic_sequence_model_event<types>{1, 1.5, 1.5}));
        in.handle(periodic_sequence_model_event<types>{4, -3.0, 1.5});
        REQUIRE(out.check(periodic_sequence_model_event<types>{0, 1.0, 1.5}));
    }

    SECTION("unsigned underflow") {
        REQUIRE_THROWS_AS(
            in.handle(periodic_sequence_model_event<types>{4, -3.01, 1.5}),
            data_validation_error);
    }
}

TEST_CASE("extrapolate periodic sequences",
          "[extrapolate_periodic_sequences]") {
    using out_events = type_list<real_one_shot_timing_event<>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         extrapolate_periodic_sequences(
                             arg::tick_index<std::size_t>{2},
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(periodic_sequence_model_event<>{42, 0.5, 1.75});
    REQUIRE(out.check(real_one_shot_timing_event<>{42, 4.0}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("add count to periodic sequences",
          "[add_count_to_periodic_sequences]") {
    using out_events = type_list<real_linear_timing_event<>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(valcat,
                         add_count_to_periodic_sequences(
                             arg::count<std::size_t>{3},
                             capture_output<out_events>(
                                 ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(periodic_sequence_model_event<>{42, 0.5, 1.75});
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
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("zero length") {
        auto in = feed_input(
            valcat, convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                        arg::count<std::size_t>{0},
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.handle(inevt{42}); // No output.
        in.handle(inevt{42}); // No output.
        in.handle(otherevt{43});
        REQUIRE(out.check(emitted_as::same_as_fed, otherevt{43}));
        in.handle(inevt{42}); // No output.
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("length 1") {
        auto in = feed_input(
            valcat, convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                        arg::count<std::size_t>{1},
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.handle(inevt{42});
        REQUIRE(out.check(startevt{42}));
        in.handle(inevt{43});
        REQUIRE(out.check(stopevt{43}));
        in.handle(inevt{44});
        REQUIRE(out.check(startevt{44}));
        in.handle(inevt{45});
        REQUIRE(out.check(stopevt{45}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("length 2") {
        auto in = feed_input(
            valcat, convert_sequences_to_start_stop<inevt, startevt, stopevt>(
                        arg::count<std::size_t>{2},
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.handle(inevt{42});
        REQUIRE(out.check(startevt{42}));
        in.handle(inevt{43});
        REQUIRE(out.check(stopevt{43}));
        REQUIRE(out.check(startevt{43}));
        in.handle(inevt{44});
        REQUIRE(out.check(stopevt{44}));

        in.handle(inevt{46});
        REQUIRE(out.check(startevt{46}));
        in.handle(inevt{47});
        REQUIRE(out.check(stopevt{47}));
        REQUIRE(out.check(startevt{47}));
        in.handle(inevt{48});
        REQUIRE(out.check(stopevt{48}));

        in.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
