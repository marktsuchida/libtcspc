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

using Open = Event<0>;
using Close = Event<1>;
using Gated = Event<2>;
using GatedSet = EventSet<Gated>;
using Other = Event<3>;
using Events = Events0123;
using OutVec = std::vector<EventVariant<Events>>;

auto MakeGateEventsFixture(bool initiallyOpen) {
    return MakeProcessorTestFixture<Events, Events>(
        [initiallyOpen](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return GateEvents<GatedSet, Open, Close, D>(initiallyOpen,
                                                        std::move(downstream));
        });
}

TEST_CASE("Gate events", "[GateEvents]") {
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
