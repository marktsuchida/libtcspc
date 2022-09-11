/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/GenerateTimings.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

using trigger = test_event<0>;
using output = test_event<1>;
using Other = test_event<2>;
using Events = test_events_0123;
using OutVec = std::vector<event_variant<Events>>;

template <typename PGen> auto MakeGenerateTimingsFixture(PGen &&generator) {
    return make_processor_test_fixture<Events, Events>(
        [&generator](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return generate_timings<trigger, PGen, D>(std::move(generator),
                                                      std::move(downstream));
        });
}

TEST_CASE("Generate null timing", "[generate_timings]") {
    auto g = null_timing_generator<output>();
    auto f = MakeGenerateTimingsFixture(std::move(g));

    f.feed_events({
        trigger{42},
    });
    REQUIRE(f.output() == OutVec{
                              trigger{42},
                          });
    f.feed_events({
        trigger{43},
    });
    REQUIRE(f.output() == OutVec{
                              trigger{43},
                          });
    f.feed_end({});
    REQUIRE(f.output() == OutVec{});
    REQUIRE(f.did_end());
}

TEST_CASE("Generate one-shot timing", "[generate_timings]") {
    macrotime delay = GENERATE(0, 1, 2);
    auto g = one_shot_timing_generator<output>(delay);
    auto f = MakeGenerateTimingsFixture(std::move(g));

    SECTION("No trigger, no output") {
        SECTION("Pass through others") {
            f.feed_events({
                Other{42},
            });
            REQUIRE(f.output() == OutVec{
                                      Other{42},
                                  });
        }
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Delayed output") {
        f.feed_events({
            trigger{42},
        });
        REQUIRE(f.output() == OutVec{
                                  trigger{42},
                              });
        SECTION("Nothing more") {}
        SECTION("Output generated") {
            if (delay > 0) {
                f.feed_events({
                    Other{42 + delay - 1},
                });
                REQUIRE(f.output() == OutVec{
                                          Other{42 + delay - 1},
                                      });
            }
            f.feed_events({
                Other{42 + delay},
            });
            REQUIRE(f.output() == OutVec{
                                      output{42 + delay},
                                      Other{42 + delay},
                                  });
        }
        SECTION("Output not generated when overlapping with next trigger") {
            f.feed_events({
                trigger{42 + delay},
            });
            REQUIRE(f.output() == OutVec{
                                      trigger{42 + delay},
                                  });
            SECTION("Nothing more") {}
            SECTION("Retrigger produces output") {
                f.feed_events({
                    Other{42 + delay + delay},
                });
                REQUIRE(f.output() == OutVec{
                                          output{42 + delay + delay},
                                          Other{42 + delay + delay},
                                      });
            }
        }
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Generate linear timing", "[generate_timings]") {
    macrotime delay = GENERATE(0, 1, 2);
    macrotime interval = GENERATE(1, 2);

    SECTION("Count of 0") {
        auto g = linear_timing_generator<output>(delay, interval, 0);
        auto f = MakeGenerateTimingsFixture(std::move(g));

        f.feed_events({
            trigger{42},
        });
        REQUIRE(f.output() == OutVec{
                                  trigger{42},
                              });
        f.feed_events({
            trigger{43 + delay},
        });
        REQUIRE(f.output() == OutVec{
                                  trigger{43 + delay},
                              });
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Count of 1") {
        auto g = linear_timing_generator<output>(delay, interval, 1);
        auto f = MakeGenerateTimingsFixture(std::move(g));

        SECTION("Delayed output") {
            f.feed_events({
                trigger{42},
            });
            REQUIRE(f.output() == OutVec{
                                      trigger{42},
                                  });
            SECTION("Nothing more") {}
            SECTION("Output generated") {
                if (delay > 0) {
                    f.feed_events({
                        Other{42 + delay - 1},
                    });
                    REQUIRE(f.output() == OutVec{
                                              Other{42 + delay - 1},
                                          });
                }
                f.feed_events({
                    Other{42 + delay},
                });
                REQUIRE(f.output() == OutVec{
                                          output{42 + delay},
                                          Other{42 + delay},
                                      });
                SECTION("Nothing more") {}
                SECTION("No second output") {
                    f.feed_events({
                        Other{42 + delay + interval + 1},
                    });
                    REQUIRE(f.output() == OutVec{
                                              Other{42 + delay + interval + 1},
                                          });
                }
            }
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
            REQUIRE(f.did_end());
        }
    }

    SECTION("Count of 2") {
        auto g = linear_timing_generator<output>(delay, interval, 2);
        auto f = MakeGenerateTimingsFixture(std::move(g));

        f.feed_events({
            trigger{42},
        });
        REQUIRE(f.output() == OutVec{
                                  trigger{42},
                              });
        if (delay > 0) {
            f.feed_events({
                Other{42 + delay - 1},
            });
            REQUIRE(f.output() == OutVec{
                                      Other{42 + delay - 1},
                                  });
        }
        f.feed_events({
            Other{42 + delay},
        });
        REQUIRE(f.output() == OutVec{
                                  output{42 + delay},
                                  Other{42 + delay},
                              });
        f.feed_events({
            Other{42 + delay + interval - 1},
        });
        REQUIRE(f.output() == OutVec{
                                  Other{42 + delay + interval - 1},
                              });
        f.feed_events({
            Other{42 + delay + interval},
        });
        REQUIRE(f.output() == OutVec{
                                  output{42 + delay + interval},
                                  Other{42 + delay + interval},
                              });
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }
}
