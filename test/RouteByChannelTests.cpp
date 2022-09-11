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

auto make_route_by_channel_fixture_output0(
    std::vector<std::int16_t> const &channels) {
    auto make_proc = [&channels](auto &&downstream) {
        using D0 = std::remove_reference_t<decltype(downstream)>;
        using D1 = discard_all<tcspc_events>;
        return route_by_channel<time_correlated_count_event, D0, D1>(
            channels, std::move(downstream), D1());
    };

    return make_processor_test_fixture<tcspc_events, tcspc_events>(make_proc);
}

auto make_route_by_channel_fixture_output1(
    std::vector<std::int16_t> const &channels) {
    auto make_proc = [&channels](auto &&downstream) {
        using D0 = discard_all<tcspc_events>;
        using D1 = std::remove_reference_t<decltype(downstream)>;
        return route_by_channel<time_correlated_count_event, D0, D1>(
            channels, D0(), std::move(downstream));
    };

    return make_processor_test_fixture<tcspc_events, tcspc_events>(make_proc);
}

auto make_route_by_channel_fixture_output2(
    std::vector<std::int16_t> const &channels) {
    auto make_proc = [&channels](auto &&downstream) {
        using D01 = discard_all<tcspc_events>;
        using D2 = std::remove_reference_t<decltype(downstream)>;
        return route_by_channel<time_correlated_count_event, D01, D01, D2>(
            channels, D01(), D01(), std::move(downstream));
    };

    return make_processor_test_fixture<tcspc_events, tcspc_events>(make_proc);
}

using out_vec = std::vector<event_variant<tcspc_events>>;

TEST_CASE("Route photons", "[route_by_channel]") {
    auto f0 = make_route_by_channel_fixture_output0({5, -3});
    f0.feed_events({
        time_correlated_count_event{{100}, 123, 5},
    });
    REQUIRE(f0.output() == out_vec{
                               time_correlated_count_event{{100}, 123, 5},
                           });
    f0.feed_events({
        time_correlated_count_event{{101}, 123, -3},
    });
    REQUIRE(f0.output() == out_vec{});
    f0.feed_events({
        time_correlated_count_event{{102}, 124, 0},
    });
    REQUIRE(f0.output() == out_vec{});
    f0.feed_events({
        marker_event{{103}, 0},
    });
    REQUIRE(f0.output() == out_vec{
                               marker_event{{103}, 0},
                           });
    f0.feed_end({});
    REQUIRE(f0.output() == out_vec{});
    REQUIRE(f0.did_end());

    auto f1 = make_route_by_channel_fixture_output1({5, -3});
    f1.feed_events({
        time_correlated_count_event{{100}, 123, 5},
    });
    REQUIRE(f1.output() == out_vec{});
    f1.feed_events({
        time_correlated_count_event{{101}, 123, -3},
    });
    REQUIRE(f1.output() == out_vec{
                               time_correlated_count_event{{101}, 123, -3},
                           });
    f1.feed_events({
        time_correlated_count_event{{102}, 124, 0},
    });
    REQUIRE(f1.output() == out_vec{});
    f1.feed_events({
        marker_event{{103}, 0},
    });
    REQUIRE(f1.output() == out_vec{
                               marker_event{{103}, 0},
                           });
    f1.feed_end({});
    REQUIRE(f1.output() == out_vec{});
    REQUIRE(f1.did_end());

    auto f2 = make_route_by_channel_fixture_output2({5, -3});
    f2.feed_events({
        time_correlated_count_event{{100}, 123, 5},
    });
    REQUIRE(f2.output() == out_vec{});
    f2.feed_events({
        time_correlated_count_event{{101}, 123, -3},
    });
    REQUIRE(f2.output() == out_vec{});
    f2.feed_events({
        time_correlated_count_event{{102}, 124, 0},
    });
    REQUIRE(f2.output() == out_vec{});
    f2.feed_events({
        marker_event{{103}, 0},
    });
    REQUIRE(f2.output() == out_vec{
                               marker_event{{103}, 0},
                           });
    f2.feed_end({});
    REQUIRE(f2.output() == out_vec{});
    REQUIRE(f2.did_end());
}
