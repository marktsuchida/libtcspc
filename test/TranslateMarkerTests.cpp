/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/TranslateMarker.hpp"

#include "FLIMEvents/TimeTaggedEvents.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

using IEvents = EventSet<MarkerEvent, Event<1>>;
using OEvents = EventSet<MarkerEvent, Event<0>, Event<1>>;
using OutVec = std::vector<EventVariant<OEvents>>;

auto MakeTranslateMarkerFixture(std::int32_t channel) {
    return MakeProcessorTestFixture<IEvents, OEvents>(
        [channel](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return TranslateMarker<MarkerEvent, Event<0>, D>(
                channel, std::move(downstream));
        });
}

TEST_CASE("Translate marker", "[TranslateMarker]") {
    auto f = MakeTranslateMarkerFixture(0);

    f.FeedEvents({
        MarkerEvent{{100}, 0},
    });
    REQUIRE(f.Output() == OutVec{
                              Event<0>{100},
                          });
    f.FeedEvents({
        MarkerEvent{{200}, 1},
    });
    REQUIRE(f.Output() == OutVec{
                              MarkerEvent{{200}, 1},
                          });
    f.FeedEvents({
        Event<1>{300},
    });
    REQUIRE(f.Output() == OutVec{
                              Event<1>{300},
                          });
    f.FeedEnd({});
    REQUIRE(f.Output() == OutVec{});
    REQUIRE(f.DidEnd());
}
