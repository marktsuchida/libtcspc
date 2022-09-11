/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/Gate.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <type_traits>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

using open_event = test_event<0>;
using close_event = test_event<1>;
using gated_event = test_event<2>;
using gated_events = event_set<gated_event>;
using other_event = test_event<3>;
using test_events = test_events_0123;
using out_vec = std::vector<event_variant<test_events>>;

auto make_gate_events_fixture(bool initially_open) {
    return make_processor_test_fixture<test_events, test_events>(
        [initially_open](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return gate_events<gated_events, open_event, close_event, D>(
                initially_open, std::move(downstream));
        });
}

TEST_CASE("Gate events", "[gate_events]") {
    bool initially_open = GENERATE(false, true);
    auto f = make_gate_events_fixture(initially_open);

    SECTION("Initial state") {
        f.feed_events({
            gated_event{},
        });
        if (initially_open) {
            REQUIRE(f.output() == out_vec{
                                      gated_event{},
                                  });
        } else {
            REQUIRE(f.output() == out_vec{});
        }
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
        REQUIRE(f.did_end());
    }

    SECTION("Pass through unrelated events") {
        f.feed_events({
            other_event{},
        });
        REQUIRE(f.output() == out_vec{
                                  other_event{},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
        REQUIRE(f.did_end());
    }

    SECTION("Pass through open/close") {
        f.feed_events({
            open_event{},
        });
        REQUIRE(f.output() == out_vec{
                                  open_event{},
                              });
        f.feed_events({
            close_event{},
        });
        REQUIRE(f.output() == out_vec{
                                  close_event{},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
        REQUIRE(f.did_end());
    }

    SECTION("Gate closed") {
        f.feed_events({
            close_event{},
        });
        REQUIRE(f.output() == out_vec{
                                  close_event{},
                              });
        f.feed_events({
            gated_event{},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
        REQUIRE(f.did_end());
    }

    SECTION("Gate open") {
        f.feed_events({
            open_event{},
        });
        REQUIRE(f.output() == out_vec{
                                  open_event{},
                              });
        f.feed_events({
            gated_event{},
        });
        REQUIRE(f.output() == out_vec{
                                  gated_event{},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
        REQUIRE(f.did_end());
    }
}
