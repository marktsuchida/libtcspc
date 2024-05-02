/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/multiplex.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "libtcspc/variant_event.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

TEST_CASE("type constraints: multiplex") {
    STATIC_CHECK(
        is_processor_v<decltype(multiplex<type_list<e0, e1>>(
                           sink_events<variant_event<type_list<e0, e1>>>())),
                       e0, e1>);
    STATIC_CHECK_FALSE(
        handles_event_v<
            decltype(multiplex<type_list<e0, e1>>(
                sink_events<variant_event<type_list<e0, e1>>, int>())),
            int>);
}

TEST_CASE("type constraints: demultiplex") {
    STATIC_CHECK(is_processor_v<decltype(demultiplex(sink_events<e0, e1>())),
                                variant_event<type_list<e0, e1>>>);
    STATIC_CHECK(handles_event_v<decltype(demultiplex(sink_events<e0, e1>())),
                                 variant_event<type_list<e0>>>);
    STATIC_CHECK_FALSE(
        handles_event_v<decltype(demultiplex(sink_events<e0>())),
                        variant_event<type_list<e0, e1>>>);
    STATIC_CHECK_FALSE(
        handles_event_v<decltype(demultiplex(sink_events<e0, e1, int>())),
                        int>);
}

TEST_CASE("introspect: multiplex") {
    check_introspect_simple_processor(multiplex<type_list<e0>>(null_sink()));
    check_introspect_simple_processor(demultiplex(null_sink()));
}

TEST_CASE("multiplex") {
    using out_events = type_list<variant_event<type_list<e0, e1>>>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, multiplex<type_list<e0, e1>>(capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(e0{});
    // TODO Should check that e0 actually got moved into the variant_event.
    REQUIRE(out.check(emitted_as::always_rvalue,
                      variant_event<type_list<e0, e1>>(e0{})));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("demultiplex") {
    using out_events = type_list<e0, e1>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in =
        feed_input(valcat, demultiplex(capture_output<out_events>(
                               ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(variant_event<type_list<e0, e1>>(e1{}));
    REQUIRE(out.check(emitted_as::same_as_fed, e1{}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
