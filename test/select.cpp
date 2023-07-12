/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/select.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

TEST_CASE("select", "[select]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        select<event_set<e0>>(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(e0{});
    REQUIRE(out.check(e0{}));
    in.feed(e1{});
    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("select_not", "[select]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(
        select_not<event_set<e0>>(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(e0{});
    in.feed(e1{});
    REQUIRE(out.check(e1{}));
    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("select_none", "[select]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(select_none(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(e0{});
    in.feed(e1{});
    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("select_all", "[select]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<e0, e1>>(select_all(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(e0{});
    REQUIRE(out.check(e0{}));
    in.feed(e1{});
    REQUIRE(out.check(e1{}));
    in.feed_end();
    REQUIRE(out.check_end());
}

} // namespace tcspc
