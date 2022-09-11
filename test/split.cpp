/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/split.hpp"

#include "flimevt/discard.hpp"

#include "processor_test_fixture.hpp"
#include "test_events.hpp"

#include <type_traits>
#include <typeinfo>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

static_assert(handles_event_set_v<
              split_events<test_events_01, discard_all<test_events_01>,
                           discard_all<test_events_01>>,
              test_events_01>);

auto make_split_events_fixture_output0() {
    auto make_proc = [](auto &&downstream) {
        using D0 = std::remove_reference_t<decltype(downstream)>;
        using D1 = discard_all<test_events_23>;
        return split_events<test_events_23, D0, D1>(std::move(downstream),
                                                    D1());
    };

    return make_processor_test_fixture<test_events_0123, test_events_01>(
        make_proc);
}

auto make_split_events_fixture_output1() {
    auto make_proc = [](auto &&downstream) {
        using D0 = discard_all<test_events_01>;
        using D1 = std::remove_reference_t<decltype(downstream)>;
        return split_events<test_events_23, D0, D1>(D0(),
                                                    std::move(downstream));
    };

    return make_processor_test_fixture<test_events_0123, test_events_23>(
        make_proc);
}

using out_vec_01 = std::vector<event_variant<test_events_01>>;
using out_vec_23 = std::vector<event_variant<test_events_23>>;

TEST_CASE("Split events", "[split_events]") {
    auto f0 = make_split_events_fixture_output0();
    auto f1 = make_split_events_fixture_output1();

    SECTION("Empty stream yields empty streams") {
        f0.feed_end({});
        REQUIRE(f0.output() == out_vec_01{});
        REQUIRE(f0.did_end());

        f1.feed_end({});
        REQUIRE(f1.output() == out_vec_23{});
        REQUIRE(f1.did_end());
    }

    SECTION("Errors propagate to both streams") {
        f0.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f0.output() == out_vec_01{});
        REQUIRE_THROWS_MATCHES(f0.did_end(), std::runtime_error,
                               Catch::Message("test"));

        f1.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f1.output() == out_vec_23{});
        REQUIRE_THROWS_MATCHES(f1.did_end(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Events are split") {
        // test_event<0> goes to output 0 only
        f0.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f0.output() == out_vec_01{
                                   test_event<0>{0},
                               });
        f1.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f1.output() == out_vec_23{});

        // test_event<0> goes to output 1 only
        f0.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f0.output() == out_vec_01{});

        f1.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f1.output() == out_vec_23{
                                   test_event<2>{0},
                               });

        f0.feed_end({});
        REQUIRE(f0.output() == out_vec_01{});
        REQUIRE(f0.did_end());

        f1.feed_end({});
        REQUIRE(f1.output() == out_vec_23{});
        REQUIRE(f1.did_end());
    }
}
