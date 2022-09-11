/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/TimeDelay.hpp"

#include "FLIMEvents/Common.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <utility>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

using Events = test_events_01;
using OutVec = std::vector<event_variant<Events>>;

auto MakeTimeDelayFixture(macrotime delta) {
    return MakeProcessorTestFixture<Events, Events>(
        [delta](auto &&downstream) {
            return time_delay(delta, std::move(downstream));
        });
}

TEST_CASE("Time delay", "[time_delay]") {
    SECTION("Zero delay is noop") {
        auto f = MakeTimeDelayFixture(0);
        f.FeedEvents({
            test_event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  test_event<0>{0},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Delay +1") {
        auto f = MakeTimeDelayFixture(1);
        f.FeedEvents({
            test_event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  test_event<0>{1},
                              });
        f.FeedEvents({
            test_event<1>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  test_event<1>{2},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Delay -1") {
        auto f = MakeTimeDelayFixture(-1);
        f.FeedEvents({
            test_event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  test_event<0>{-1},
                              });
        f.FeedEvents({
            test_event<1>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  test_event<1>{0},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }
}
