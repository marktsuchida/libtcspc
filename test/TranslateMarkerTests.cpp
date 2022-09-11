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

using inputs = event_set<marker_event, test_event<1>>;
using outputs = event_set<marker_event, test_event<0>, test_event<1>>;
using out_vec = std::vector<event_variant<outputs>>;

auto make_translate_marker_fixture(std::int32_t channel) {
    return make_processor_test_fixture<inputs, outputs>(
        [channel](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return translate_marker<marker_event, test_event<0>, D>(
                channel, std::move(downstream));
        });
}

TEST_CASE("Translate marker", "[translate_marker]") {
    auto f = make_translate_marker_fixture(0);

    f.feed_events({
        marker_event{{100}, 0},
    });
    REQUIRE(f.output() == out_vec{
                              test_event<0>{100},
                          });
    f.feed_events({
        marker_event{{200}, 1},
    });
    REQUIRE(f.output() == out_vec{
                              marker_event{{200}, 1},
                          });
    f.feed_events({
        test_event<1>{300},
    });
    REQUIRE(f.output() == out_vec{
                              test_event<1>{300},
                          });
    f.feed_end({});
    REQUIRE(f.output() == out_vec{});
    REQUIRE(f.did_end());
}
