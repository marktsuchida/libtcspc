/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/multiplex.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

namespace {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

} // namespace

TEST_CASE("multiplex", "[multiplex]") {
    auto out = capture_output<event_set<event_variant<event_set<e0, e1>>>>();
    auto in = feed_input<event_set<e0, e1>>(
        multiplex<event_set<e0, e1>>(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(e0{});
    REQUIRE(out.check(event_variant<event_set<e0, e1>>(e0{})));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("demultiplex", "[demultiplex]") {
    auto out = capture_output<event_set<e0, e1>>();
    auto in = feed_input<event_set<event_variant<event_set<e0, e1>>>>(
        demultiplex<event_set<e0, e1>>(ref_processor(out)));
    in.require_output_checked(out);

    in.feed(event_variant<event_set<e0, e1>>(e1{}));
    REQUIRE(out.check(e1{}));
    in.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
