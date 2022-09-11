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

using Open = test_event<0>;
using Close = test_event<1>;
using Gated = test_event<2>;
using GatedSet = event_set<Gated>;
using Other = test_event<3>;
using Events = test_events_0123;
using OutVec = std::vector<event_variant<Events>>;

auto MakeGateEventsFixture(bool initiallyOpen) {
    return make_processor_test_fixture<Events, Events>(
        [initiallyOpen](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return gate_events<GatedSet, Open, Close, D>(
                initiallyOpen, std::move(downstream));
        });
}

TEST_CASE("Gate events", "[gate_events]") {
    bool initiallyOpen = GENERATE(false, true);
    auto f = MakeGateEventsFixture(initiallyOpen);

    SECTION("Initial state") {
        f.feed_events({
            Gated{},
        });
        if (initiallyOpen) {
            REQUIRE(f.output() == OutVec{
                                      Gated{},
                                  });
        } else {
            REQUIRE(f.output() == OutVec{});
        }
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Pass through unrelated events") {
        f.feed_events({
            Other{},
        });
        REQUIRE(f.output() == OutVec{
                                  Other{},
                              });
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Pass through open/close") {
        f.feed_events({
            Open{},
        });
        REQUIRE(f.output() == OutVec{
                                  Open{},
                              });
        f.feed_events({
            Close{},
        });
        REQUIRE(f.output() == OutVec{
                                  Close{},
                              });
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Gate closed") {
        f.feed_events({
            Close{},
        });
        REQUIRE(f.output() == OutVec{
                                  Close{},
                              });
        f.feed_events({
            Gated{},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Gate open") {
        f.feed_events({
            Open{},
        });
        REQUIRE(f.output() == OutVec{
                                  Open{},
                              });
        f.feed_events({
            Gated{},
        });
        REQUIRE(f.output() == OutVec{
                                  Gated{},
                              });
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }
}
