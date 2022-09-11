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
    return MakeProcessorTestFixture<Events, Events>(
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
        f.FeedEvents({
            Gated{},
        });
        if (initiallyOpen) {
            REQUIRE(f.Output() == OutVec{
                                      Gated{},
                                  });
        } else {
            REQUIRE(f.Output() == OutVec{});
        }
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Pass through unrelated events") {
        f.FeedEvents({
            Other{},
        });
        REQUIRE(f.Output() == OutVec{
                                  Other{},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Pass through open/close") {
        f.FeedEvents({
            Open{},
        });
        REQUIRE(f.Output() == OutVec{
                                  Open{},
                              });
        f.FeedEvents({
            Close{},
        });
        REQUIRE(f.Output() == OutVec{
                                  Close{},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Gate closed") {
        f.FeedEvents({
            Close{},
        });
        REQUIRE(f.Output() == OutVec{
                                  Close{},
                              });
        f.FeedEvents({
            Gated{},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Gate open") {
        f.FeedEvents({
            Open{},
        });
        REQUIRE(f.Output() == OutVec{
                                  Open{},
                              });
        f.FeedEvents({
            Gated{},
        });
        REQUIRE(f.Output() == OutVec{
                                  Gated{},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }
}
