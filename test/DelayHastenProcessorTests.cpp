/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/DelayHastenProcessor.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

static_assert(handles_event_set_v<
              delay_processor<test_events_01, discard_all<test_events_0123>>,
              test_events_0123>);
static_assert(handles_event_set_v<
              hasten_processor<test_events_01, discard_all<test_events_0123>>,
              test_events_0123>);
static_assert(
    handles_event_set_v<delay_hasten_processor<test_events_01, test_events_23,
                                               discard_all<test_events_0123>>,
                        test_events_0123>);

// Make fixture to test delaying test_events_01
auto MakeDelayFixture(macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return delay_processor<test_events_01, D>(delta,
                                                  std::move(downstream));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        makeProc);
}

// Make fixture to test hastening Event01
auto MakeHastenFixture(macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return hasten_processor<test_events_23, D>(delta,
                                                   std::move(downstream));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        makeProc);
}

auto MakeDelayHastenFixture(macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return delay_hasten_processor<test_events_01, test_events_23, D>(
            delta, std::move(downstream));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        makeProc);
}

using OutVec = std::vector<flimevt::event_variant<test_events_0123>>;

TEST_CASE("Delay uniform streams", "[delay_processor]") {
    macrotime delta = GENERATE(0, 1, 2);
    auto f = MakeDelayFixture(delta);

    SECTION("Empty stream yields empty stream") {
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Empty stream with error yields empty stream with error") {
        f.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f.output() == OutVec{});
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Undelayed events are unbuffered") {
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                              });
        f.feed_events({
            test_event<3>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<3>{0},
                              });
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                              });
        f.feed_events({
            test_event<3>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<3>{0},
                              });
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Delayed events are buffered") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<1>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<1>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{delta},
                                  test_event<1>{delta},
                                  test_event<0>{delta},
                                  test_event<1>{delta},
                              });
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Hasten uniform streams", "[hasten_processor]") {
    macrotime delta = GENERATE(0, 1, 2);
    auto f = MakeHastenFixture(delta);

    SECTION("Empty stream yields empty stream") {
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Empty stream with error yields empty stream with error") {
        f.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f.output() == OutVec{});
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Hastened events are unbuffered") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{-delta},
                              });
        f.feed_events({
            test_event<1>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<1>{-delta},
                              });
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{-delta},
                              });
        f.feed_events({
            test_event<1>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<1>{-delta},
                              });
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Unhastened events are buffered") {
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<3>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<3>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                                  test_event<3>{0},
                                  test_event<2>{0},
                                  test_event<3>{0},
                              });
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Delay by 0", "[delay_processor]") {
    auto f = MakeDelayFixture(0);

    SECTION("Equal timestamps") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{0},
                                  test_event<2>{0},
                              });

        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{0},
                                  test_event<2>{0},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }

    SECTION("Increment of 1") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{0},
                                  test_event<2>{1},
                              });

        f.feed_events({
            test_event<0>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{3},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{2},
                                  test_event<2>{3},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Hasten by 0", "[hasten_processor]") {
    auto f = MakeHastenFixture(0);

    SECTION("Equal timestamps") {
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{0},
                              });

        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{0},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                                  test_event<2>{0},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Increment of 1") {
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                                  test_event<0>{1},
                              });

        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{3},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{2},
                                  test_event<0>{3},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Delay by 1") {
    auto f = MakeDelayFixture(1);

    SECTION("Equal timestamps") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                              });

        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{1},
                                  test_event<2>{1},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{2},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Increment of 1") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{1},
                                  test_event<2>{1},
                              });

        f.feed_events({
            test_event<0>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{3},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{3},
                                  test_event<2>{3},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Hasten by 1") {
    auto f = MakeHastenFixture(1);

    SECTION("Equal timestamps") {
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{-1},
                              });

        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{0},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                                  test_event<2>{1},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Increment of 1") {
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{0},
                              });

        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{3},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                                  test_event<0>{2},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{2},
                              });
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Delay by 2") {
    auto f = MakeDelayFixture(2);

    SECTION("Equal timestamps") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                              });

        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{1},
                              });

        f.feed_events({
            test_event<0>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{2},
                                  test_event<2>{2},
                              });

        f.feed_events({
            test_event<2>{3},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{3},
                                  test_event<2>{3},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{4},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Increment of 1") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{1},
                              });
        f.feed_events({
            test_event<0>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{3},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{2},
                                  test_event<2>{3},
                              });

        f.feed_events({
            test_event<0>{4},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{5},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{4},
                                  test_event<2>{5},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{6},
                              });
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Hasten by 2") {
    auto f = MakeHastenFixture(2);

    SECTION("Equal timestamps") {
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{-2},
                              });

        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{-1},
                              });

        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{2},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{0},
                              });

        f.feed_events({
            test_event<0>{3},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                                  test_event<0>{1},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{1},
                                  test_event<2>{2},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Increment of 1") {
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{-1},
                              });
        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{3},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{0},
                                  test_event<0>{1},
                              });

        f.feed_events({
            test_event<2>{4},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{5},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{2},
                                  test_event<0>{3},
                              });

        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{4},
                              });
        REQUIRE(f.did_end());
    }
}

TEST_CASE("delay_hasten_processor Sanity", "[delay_hasten_processor]") {
    macrotime delta = GENERATE(-2, -1, 0, 1, 2);
    auto f = MakeDelayHastenFixture(delta);

    // Ignore output timing; only check content.

    f.feed_events({
        test_event<2>{-3},
        test_event<0>{0},
        test_event<2>{3},
        test_event<0>{6},
    });
    OutVec o = f.output();
    f.feed_end({});
    OutVec o1 = f.output();

    std::copy(o1.cbegin(), o1.cend(), std::back_inserter(o));

    REQUIRE(o == OutVec{
                     test_event<2>{-3},
                     test_event<0>{0 + delta},
                     test_event<2>{3},
                     test_event<0>{6 + delta},
                 });

    REQUIRE(f.did_end());
}
