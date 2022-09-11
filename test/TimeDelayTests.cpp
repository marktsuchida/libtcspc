/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/time_delay.hpp"

#include "flimevt/common.hpp"

#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <utility>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

using test_events = test_events_01;
using out_vec = std::vector<event_variant<test_events>>;

auto make_time_delay_fixture(macrotime delta) {
    return make_processor_test_fixture<test_events, test_events>(
        [delta](auto &&downstream) {
            return time_delay(delta, std::move(downstream));
        });
}

TEST_CASE("Time delay", "[time_delay]") {
    SECTION("Zero delay is noop") {
        auto f = make_time_delay_fixture(0);
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{0},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
        REQUIRE(f.did_end());
    }

    SECTION("Delay +1") {
        auto f = make_time_delay_fixture(1);
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{1},
                              });
        f.feed_events({
            test_event<1>{1},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<1>{2},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
        REQUIRE(f.did_end());
    }

    SECTION("Delay -1") {
        auto f = make_time_delay_fixture(-1);
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{-1},
                              });
        f.feed_events({
            test_event<1>{1},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<1>{0},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
        REQUIRE(f.did_end());
    }
}
