/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/multiplex.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "libtcspc/variant_event.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

TEST_CASE("introspect multiplex", "[introspect]") {
    check_introspect_simple_processor(multiplex<type_list<e0>>(null_sink()));
    check_introspect_simple_processor(demultiplex(null_sink()));
}

TEST_CASE("multiplex") {
    using out_events = type_list<variant_event<type_list<e0, e1>>>;
    auto ctx = context::create();
    auto in = feed_input<type_list<e0, e1>>(
        multiplex<type_list<e0, e1>>(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(e0{});
    REQUIRE(out.check(variant_event<type_list<e0, e1>>(e0{})));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("demultiplex handled event types") {
    demultiplex(sink_events<type_list<e0>>())
        .handle(variant_event<type_list<e0>>{e0{}});
    demultiplex(sink_events<type_list<e0, e1>>())
        .handle(variant_event<type_list<e0>>{e0{}});
    demultiplex(sink_events<type_list<e0, e1>>())
        .handle(variant_event<type_list<e1>>{e1{}});
    demultiplex(sink_events<type_list<e0, e1>>())
        .handle(variant_event<type_list<e1, e0>>{e1{}});
}

TEST_CASE("demultiplex") {
    using out_events = type_list<e0, e1>;
    auto ctx = context::create();
    auto in = feed_input<type_list<variant_event<type_list<e0, e1>>>>(
        demultiplex(capture_output<out_events>(
            ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(variant_event<type_list<e0, e1>>(e1{}));
    REQUIRE(out.check(e1{}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
