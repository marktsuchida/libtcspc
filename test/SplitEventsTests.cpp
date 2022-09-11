/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/SplitEvents.hpp"

#include "FLIMEvents/Discard.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <type_traits>
#include <typeinfo>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

static_assert(handles_event_set_v<
              split_events<test_events_01, discard_all<test_events_01>,
                           discard_all<test_events_01>>,
              test_events_01>);

auto MakeSplitEventsFixtureOutput0() {
    auto makeProc = [](auto &&downstream) {
        using D0 = std::remove_reference_t<decltype(downstream)>;
        using D1 = discard_all<test_events_23>;
        return split_events<test_events_23, D0, D1>(std::move(downstream),
                                                    D1());
    };

    return MakeProcessorTestFixture<test_events_0123, test_events_01>(
        makeProc);
}

auto MakeSplitEventsFixtureOutput1() {
    auto makeProc = [](auto &&downstream) {
        using D0 = discard_all<test_events_01>;
        using D1 = std::remove_reference_t<decltype(downstream)>;
        return split_events<test_events_23, D0, D1>(D0(),
                                                    std::move(downstream));
    };

    return MakeProcessorTestFixture<test_events_0123, test_events_23>(
        makeProc);
}

using OutVec01 = std::vector<event_variant<test_events_01>>;
using OutVec23 = std::vector<event_variant<test_events_23>>;

TEST_CASE("Split events", "[split_events]") {
    auto f0 = MakeSplitEventsFixtureOutput0();
    auto f1 = MakeSplitEventsFixtureOutput1();

    SECTION("Empty stream yields empty streams") {
        f0.FeedEnd({});
        REQUIRE(f0.Output() == OutVec01{});
        REQUIRE(f0.DidEnd());

        f1.FeedEnd({});
        REQUIRE(f1.Output() == OutVec23{});
        REQUIRE(f1.DidEnd());
    }

    SECTION("Errors propagate to both streams") {
        f0.FeedEnd(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f0.Output() == OutVec01{});
        REQUIRE_THROWS_MATCHES(f0.DidEnd(), std::runtime_error,
                               Catch::Message("test"));

        f1.FeedEnd(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f1.Output() == OutVec23{});
        REQUIRE_THROWS_MATCHES(f1.DidEnd(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Events are split") {
        // test_event<0> goes to output 0 only
        f0.FeedEvents({
            test_event<0>{0},
        });
        REQUIRE(f0.Output() == OutVec01{
                                   test_event<0>{0},
                               });
        f1.FeedEvents({
            test_event<0>{0},
        });
        REQUIRE(f1.Output() == OutVec23{});

        // test_event<0> goes to output 1 only
        f0.FeedEvents({
            test_event<2>{0},
        });
        REQUIRE(f0.Output() == OutVec01{});

        f1.FeedEvents({
            test_event<2>{0},
        });
        REQUIRE(f1.Output() == OutVec23{
                                   test_event<2>{0},
                               });

        f0.FeedEnd({});
        REQUIRE(f0.Output() == OutVec01{});
        REQUIRE(f0.DidEnd());

        f1.FeedEnd({});
        REQUIRE(f1.Output() == OutVec23{});
        REQUIRE(f1.DidEnd());
    }
}
