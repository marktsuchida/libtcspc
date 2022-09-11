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

using IEvents = event_set<marker_event, Event<1>>;
using OEvents = event_set<marker_event, Event<0>, Event<1>>;
using OutVec = std::vector<event_variant<OEvents>>;

auto MakeTranslateMarkerFixture(std::int32_t channel) {
    return MakeProcessorTestFixture<IEvents, OEvents>(
        [channel](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return translate_marker<marker_event, Event<0>, D>(
                channel, std::move(downstream));
        });
}

TEST_CASE("Translate marker", "[translate_marker]") {
    auto f = MakeTranslateMarkerFixture(0);

    f.FeedEvents({
        marker_event{{100}, 0},
    });
    REQUIRE(f.Output() == OutVec{
                              Event<0>{100},
                          });
    f.FeedEvents({
        marker_event{{200}, 1},
    });
    REQUIRE(f.Output() == OutVec{
                              marker_event{{200}, 1},
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
