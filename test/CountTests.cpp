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

using Input = test_event<0>;
using output = test_event<1>;
using Reset = test_event<2>;
using Other = test_event<3>;
using Events = test_events_0123;
using OutVec = std::vector<event_variant<Events>>;

template <bool EmitAfter>
auto MakeCountEventFixture(std::uint64_t threshold, std::uint64_t limit) {
    return make_processor_test_fixture<Events, Events>(
        [threshold, limit](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return count_event<Input, Reset, output, EmitAfter, D>(
                threshold, limit, std::move(downstream));
        });
}

TEST_CASE("Count event", "[count_event]") {
    SECTION("Threshold 0, limit 1") {
        SECTION("Emit before") {
            auto f = MakeCountEventFixture<false>(0, 1);
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      output{42},
                                      Input{42},
                                  });
            f.feed_events({
                Input{43},
            });
            REQUIRE(f.output() == OutVec{
                                      output{43},
                                      Input{43},
                                  });
            f.feed_events({
                Reset{44},
            });
            REQUIRE(f.output() == OutVec{
                                      Reset{44},
                                  });
            f.feed_events({
                Input{45},
            });
            REQUIRE(f.output() == OutVec{
                                      output{45},
                                      Input{45},
                                  });
            f.feed_events({
                Other{46},
            });
            REQUIRE(f.output() == OutVec{
                                      Other{46},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
            REQUIRE(f.did_end());
        }

        SECTION("Emit after") {
            auto f = MakeCountEventFixture<true>(0, 1);
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{42},
                                  });
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{42},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
            REQUIRE(f.did_end());
        }
    }

    SECTION("Threshold 1, limit 1") {
        SECTION("Emit before") {
            auto f = MakeCountEventFixture<false>(1, 1);
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{42},
                                  });
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{42},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
            REQUIRE(f.did_end());
        }

        SECTION("Emit after") {
            auto f = MakeCountEventFixture<true>(1, 1);
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{42},
                                      output{42},
                                  });
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{42},
                                      output{42},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
            REQUIRE(f.did_end());
        }
    }

    SECTION("Threshold 1, limit 2") {
        SECTION("Emit before") {
            auto f = MakeCountEventFixture<false>(1, 2);
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{42},
                                  });
            f.feed_events({
                Input{43},
            });
            REQUIRE(f.output() == OutVec{
                                      output{43},
                                      Input{43},
                                  });
            f.feed_events({
                Input{44},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{44},
                                  });
            f.feed_events({
                Reset{},
            });
            REQUIRE(f.output() == OutVec{
                                      Reset{},
                                  });
            f.feed_events({
                Input{45},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{45},
                                  });
            f.feed_events({
                Input{46},
            });
            REQUIRE(f.output() == OutVec{
                                      output{46},
                                      Input{46},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
            REQUIRE(f.did_end());
        }

        SECTION("Emit after") {
            auto f = MakeCountEventFixture<true>(1, 2);
            f.feed_events({
                Input{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{42},
                                      output{42},
                                  });
            f.feed_events({
                Input{43},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{43},
                                  });
            f.feed_events({
                Input{44},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{44},
                                      output{44},
                                  });
            f.feed_events({
                Reset{},
            });
            REQUIRE(f.output() == OutVec{
                                      Reset{},
                                  });
            f.feed_events({
                Input{45},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{45},
                                      output{45},
                                  });
            f.feed_events({
                Input{46},
            });
            REQUIRE(f.output() == OutVec{
                                      Input{46},
                                  });
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
            REQUIRE(f.did_end());
        }
    }
}
