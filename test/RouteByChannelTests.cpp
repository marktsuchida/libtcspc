/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/RouteByChannel.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/TimeTaggedEvents.hpp"
#include "ProcessorTestFixture.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

auto MakeRouteByChannelFixtureOutput0(
    std::vector<std::int16_t> const &channels) {
    auto makeProc = [&channels](auto &&downstream) {
        using D0 = std::remove_reference_t<decltype(downstream)>;
        using D1 = DiscardAll<TCSPCEvents>;
        return RouteByChannel<TimeCorrelatedCountEvent, D0, D1>(
            channels, std::move(downstream), D1());
    };

    return MakeProcessorTestFixture<TCSPCEvents, TCSPCEvents>(makeProc);
}

auto MakeRouteByChannelFixtureOutput1(
    std::vector<std::int16_t> const &channels) {
    auto makeProc = [&channels](auto &&downstream) {
        using D0 = DiscardAll<TCSPCEvents>;
        using D1 = std::remove_reference_t<decltype(downstream)>;
        return RouteByChannel<TimeCorrelatedCountEvent, D0, D1>(
            channels, D0(), std::move(downstream));
    };

    return MakeProcessorTestFixture<TCSPCEvents, TCSPCEvents>(makeProc);
}

auto MakeRouteByChannelFixtureOutput2(
    std::vector<std::int16_t> const &channels) {
    auto makeProc = [&channels](auto &&downstream) {
        using D01 = DiscardAll<TCSPCEvents>;
        using D2 = std::remove_reference_t<decltype(downstream)>;
        return RouteByChannel<TimeCorrelatedCountEvent, D01, D01, D2>(
            channels, D01(), D01(), std::move(downstream));
    };

    return MakeProcessorTestFixture<TCSPCEvents, TCSPCEvents>(makeProc);
}

using OutVec = std::vector<EventVariant<TCSPCEvents>>;

TEST_CASE("Route photons", "[RouteByChannel]") {
    auto f0 = MakeRouteByChannelFixtureOutput0({5, -3});
    f0.FeedEvents({
        TimeCorrelatedCountEvent{{100}, 123, 5},
    });
    REQUIRE(f0.Output() == OutVec{
                               TimeCorrelatedCountEvent{{100}, 123, 5},
                           });
    f0.FeedEvents({
        TimeCorrelatedCountEvent{{101}, 123, -3},
    });
    REQUIRE(f0.Output() == OutVec{});
    f0.FeedEvents({
        TimeCorrelatedCountEvent{{102}, 124, 0},
    });
    REQUIRE(f0.Output() == OutVec{});
    f0.FeedEvents({
        MarkerEvent{{103}, 0},
    });
    REQUIRE(f0.Output() == OutVec{
                               MarkerEvent{{103}, 0},
                           });
    f0.FeedEnd({});
    REQUIRE(f0.Output() == OutVec{});
    REQUIRE(f0.DidEnd());

    auto f1 = MakeRouteByChannelFixtureOutput1({5, -3});
    f1.FeedEvents({
        TimeCorrelatedCountEvent{{100}, 123, 5},
    });
    REQUIRE(f1.Output() == OutVec{});
    f1.FeedEvents({
        TimeCorrelatedCountEvent{{101}, 123, -3},
    });
    REQUIRE(f1.Output() == OutVec{
                               TimeCorrelatedCountEvent{{101}, 123, -3},
                           });
    f1.FeedEvents({
        TimeCorrelatedCountEvent{{102}, 124, 0},
    });
    REQUIRE(f1.Output() == OutVec{});
    f1.FeedEvents({
        MarkerEvent{{103}, 0},
    });
    REQUIRE(f1.Output() == OutVec{
                               MarkerEvent{{103}, 0},
                           });
    f1.FeedEnd({});
    REQUIRE(f1.Output() == OutVec{});
    REQUIRE(f1.DidEnd());

    auto f2 = MakeRouteByChannelFixtureOutput2({5, -3});
    f2.FeedEvents({
        TimeCorrelatedCountEvent{{100}, 123, 5},
    });
    REQUIRE(f2.Output() == OutVec{});
    f2.FeedEvents({
        TimeCorrelatedCountEvent{{101}, 123, -3},
    });
    REQUIRE(f2.Output() == OutVec{});
    f2.FeedEvents({
        TimeCorrelatedCountEvent{{102}, 124, 0},
    });
    REQUIRE(f2.Output() == OutVec{});
    f2.FeedEvents({
        MarkerEvent{{103}, 0},
    });
    REQUIRE(f2.Output() == OutVec{
                               MarkerEvent{{103}, 0},
                           });
    f2.FeedEnd({});
    REQUIRE(f2.Output() == OutVec{});
    REQUIRE(f2.DidEnd());
}
