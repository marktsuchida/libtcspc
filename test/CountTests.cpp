/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/Count.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <cstdint>
#include <type_traits>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

using input_event = test_event<0>;
using output_event = test_event<1>;
using reset_event = test_event<2>;
using other_event = test_event<3>;
using test_events = test_events_0123;
using out_vec = std::vector<event_variant<test_events>>;

template <bool EmitAfter>
auto make_count_event_fixture(std::uint64_t threshold, std::uint64_t limit) {
    return make_processor_test_fixture<test_events, test_events>(
        [threshold, limit](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return count_event<input_event, reset_event, output_event,
                               EmitAfter, D>(threshold, limit,
                                             std::move(downstream));
        });
}

TEST_CASE("Count event", "[count_event]") {
    SECTION("Threshold 0, limit 1") {
        SECTION("Emit before") {
            auto f = make_count_event_fixture<false>(0, 1);
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      output_event{42},
                                      input_event{42},
                                  });
            f.feed_events({
                input_event{43},
            });
            REQUIRE(f.output() == out_vec{
                                      output_event{43},
                                      input_event{43},
                                  });
            f.feed_events({
                reset_event{44},
            });
            REQUIRE(f.output() == out_vec{
                                      reset_event{44},
                                  });
            f.feed_events({
                input_event{45},
            });
            REQUIRE(f.output() == out_vec{
                                      output_event{45},
                                      input_event{45},
                                  });
            f.feed_events({
                other_event{46},
            });
            REQUIRE(f.output() == out_vec{
                                      other_event{46},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
            REQUIRE(f.did_end());
        }

        SECTION("Emit after") {
            auto f = make_count_event_fixture<true>(0, 1);
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{42},
                                  });
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{42},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
            REQUIRE(f.did_end());
        }
    }

    SECTION("Threshold 1, limit 1") {
        SECTION("Emit before") {
            auto f = make_count_event_fixture<false>(1, 1);
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{42},
                                  });
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{42},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
            REQUIRE(f.did_end());
        }

        SECTION("Emit after") {
            auto f = make_count_event_fixture<true>(1, 1);
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{42},
                                      output_event{42},
                                  });
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{42},
                                      output_event{42},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
            REQUIRE(f.did_end());
        }
    }

    SECTION("Threshold 1, limit 2") {
        SECTION("Emit before") {
            auto f = make_count_event_fixture<false>(1, 2);
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{42},
                                  });
            f.feed_events({
                input_event{43},
            });
            REQUIRE(f.output() == out_vec{
                                      output_event{43},
                                      input_event{43},
                                  });
            f.feed_events({
                input_event{44},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{44},
                                  });
            f.feed_events({
                reset_event{},
            });
            REQUIRE(f.output() == out_vec{
                                      reset_event{},
                                  });
            f.feed_events({
                input_event{45},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{45},
                                  });
            f.feed_events({
                input_event{46},
            });
            REQUIRE(f.output() == out_vec{
                                      output_event{46},
                                      input_event{46},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
            REQUIRE(f.did_end());
        }

        SECTION("Emit after") {
            auto f = make_count_event_fixture<true>(1, 2);
            f.feed_events({
                input_event{42},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{42},
                                      output_event{42},
                                  });
            f.feed_events({
                input_event{43},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{43},
                                  });
            f.feed_events({
                input_event{44},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{44},
                                      output_event{44},
                                  });
            f.feed_events({
                reset_event{},
            });
            REQUIRE(f.output() == out_vec{
                                      reset_event{},
                                  });
            f.feed_events({
                input_event{45},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{45},
                                      output_event{45},
                                  });
            f.feed_events({
                input_event{46},
            });
            REQUIRE(f.output() == out_vec{
                                      input_event{46},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
            REQUIRE(f.did_end());
        }
    }
}
