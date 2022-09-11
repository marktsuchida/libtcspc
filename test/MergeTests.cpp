/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/Merge.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/SplitEvents.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <type_traits>
#include <typeinfo>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

static_assert(
    handles_event_set_v<decltype(make_merge<test_events_0123>(
                                     0, discard_all<test_events_0123>())
                                     .first),
                        test_events_0123>);

static_assert(
    handles_event_set_v<decltype(make_merge<test_events_0123>(
                                     0, discard_all<test_events_0123>())
                                     .second),
                        test_events_0123>);

// Instead of coming up with a 2-input test fixture, we rely on split_events
// for the input.
auto MakeMergeFixture(macrotime maxShift) {
    auto makeProc = [maxShift](auto &&downstream) {
        auto [input0, input1] =
            make_merge<test_events_0123>(maxShift, std::move(downstream));
        return split_events<test_events_23, decltype(input0),
                            decltype(input1)>(std::move(input0),
                                              std::move(input1));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        makeProc);
}

// Processor to inject an error after N events are passed through
template <typename D> class InjectError {
    int eventsToEmitBeforeError;
    bool finished = false;
    D downstream;

  public:
    explicit InjectError(int eventsBeforeError, D &&downstream)
        : eventsToEmitBeforeError(eventsBeforeError),
          downstream(std::move(downstream)) {}

    template <typename E> void handle_event(E const &event) noexcept {
        if (eventsToEmitBeforeError-- > 0) {
            downstream.handle_event(event);
        } else {
            downstream.handle_end(
                std::make_exception_ptr(std::runtime_error("injected error")));
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (eventsToEmitBeforeError > 0)
            downstream.handle_end(error);
    }
};

auto MakeMergeFixtureErrorOnInput0(macrotime maxShift, int eventsBeforeError) {
    auto makeProc = [maxShift, eventsBeforeError](auto &&downstream) {
        auto [input0, input1] =
            make_merge<test_events_0123>(maxShift, std::move(downstream));
        auto error0 = InjectError(eventsBeforeError, std::move(input0));
        return split_events<test_events_23, decltype(error0),
                            decltype(input1)>(std::move(error0),
                                              std::move(input1));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        makeProc);
}

auto MakeMergeFixtureErrorOnInput1(macrotime maxShift, int eventsBeforeError) {
    auto makeProc = [maxShift, eventsBeforeError](auto &&downstream) {
        auto [input0, input1] =
            make_merge<test_events_0123>(maxShift, std::move(downstream));
        auto error1 = InjectError(eventsBeforeError, std::move(input1));
        return split_events<test_events_23, decltype(input0),
                            decltype(error1)>(std::move(input0),
                                              std::move(error1));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        makeProc);
}

using OutVec = std::vector<event_variant<test_events_0123>>;

TEST_CASE("Merge with error on one input", "[Merge]") {
    SECTION("Input0 error with no events pending") {
        bool furtherInputOnInput1 = GENERATE(false, true);
        bool endInput1 = GENERATE(false, true);

        auto f = MakeMergeFixtureErrorOnInput0(1000, 0);
        f.feed_events({
            test_event<0>{0}, // Fixture will convert to error
        });
        REQUIRE(f.output() == OutVec{});
        if (furtherInputOnInput1) {
            f.feed_events({
                test_event<2>{1},
            });
            REQUIRE(f.output() == OutVec{});
        }
        if (endInput1) {
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
        }
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("injected error"));
    }

    SECTION("Input1 error with no events pending") {
        bool furtherInputOnInput0 = GENERATE(false, true);
        bool endInput0 = GENERATE(false, true);

        auto f = MakeMergeFixtureErrorOnInput1(1000, 0);
        f.feed_events({
            test_event<2>{0}, // Fixture will convert to error
        });
        REQUIRE(f.output() == OutVec{});
        if (furtherInputOnInput0) {
            f.feed_events({
                test_event<0>{1}, // Further input ignored on other input
            });
            REQUIRE(f.output() == OutVec{});
        }
        if (endInput0) {
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
        }
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("injected error"));
    }

    SECTION("Input0 error with input0 events pending") {
        bool endInput1 = GENERATE(false, true);

        auto f = MakeMergeFixtureErrorOnInput0(1000, 1);
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{1}, // Fixture will convert to error
        });
        REQUIRE(f.output() == OutVec{});
        if (endInput1) {
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
        }
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("injected error"));
    }

    SECTION("Input0 error with input1 events pending") {
        bool endInput1 = GENERATE(false, true);

        auto f = MakeMergeFixtureErrorOnInput0(1000, 0);
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{1}, // Fixture will convert to error
        });
        REQUIRE(f.output() == OutVec{});
        if (endInput1) {
            f.feed_end({});
            REQUIRE(f.output() == OutVec{});
        }
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("injected error"));
    }
}

TEST_CASE("Merge", "[Merge]") {
    auto f = MakeMergeFixture(1000);

    SECTION("Empty streams yield empty stream") {
        f.feed_end({});
        REQUIRE(f.output() == OutVec{});
    }

    SECTION("Errors on both inputs") {
        f.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f.output() == OutVec{});
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Input0 events are emitted before input1 events") {
        f.feed_events({
            test_event<2>{42},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{42},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{42},
                              });
        f.feed_events({
            test_event<3>{42},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<1>{42},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<1>{42},
                              });
        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{42},
                                  test_event<3>{42},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Already sorted in macrotime order") {
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == OutVec{test_event<0>{1}});
        f.feed_events({
            test_event<0>{3},
        });
        REQUIRE(f.output() == OutVec{test_event<2>{2}});
        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{3},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Delayed input0 sorted by macrotime") {
        f.feed_events({
            test_event<0>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{1},
                              });
        f.feed_events({
            test_event<0>{4},
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
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{4},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Delayed input1 sorted by macrotime") {
        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{1},
                              });
        f.feed_events({
            test_event<2>{4},
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
        REQUIRE(f.output() == OutVec{
                                  test_event<2>{4},
                              });
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Merge max time shift", "[Merge]") {
    auto f = MakeMergeFixture(10);

    SECTION("Input0 emitted after exceeding max time shift") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{10},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<0>{11},
        });
        REQUIRE(f.output() == OutVec{test_event<0>{0}});
        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<0>{10},
                                  test_event<0>{11},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Input1 emitted after exceeding max time shift") {
        f.feed_events({
            test_event<1>{0},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<1>{10},
        });
        REQUIRE(f.output() == OutVec{});
        f.feed_events({
            test_event<1>{11},
        });
        REQUIRE(f.output() == OutVec{test_event<1>{0}});
        f.feed_end({});
        REQUIRE(f.output() == OutVec{
                                  test_event<1>{10},
                                  test_event<1>{11},
                              });
        REQUIRE(f.did_end());
    }
}
