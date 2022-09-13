/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/merge.hpp"

#include "flimevt/discard.hpp"
#include "flimevt/split.hpp"

#include "processor_test_fixture.hpp"
#include "test_events.hpp"

#include <type_traits>
#include <typeinfo>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

static_assert(
    handles_event_set_v<
        decltype(merge<test_events_0123>(0, discard_all<test_events_0123>())
                     .first),
        test_events_0123>);

static_assert(
    handles_event_set_v<
        decltype(merge<test_events_0123>(0, discard_all<test_events_0123>())
                     .second),
        test_events_0123>);

// Instead of coming up with a 2-input test fixture, we rely on split_events
// for the input.
auto make_merge_fixture(macrotime max_shift) {
    auto make_proc = [max_shift](auto &&downstream) {
        auto [input0, input1] =
            merge<test_events_0123>(max_shift, std::move(downstream));
        return split_events<test_events_23>(std::move(input0),
                                            std::move(input1));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        make_proc);
}

// Processor to inject an error after N events are passed through
template <typename D> class inject_error {
    int events_to_emit_before_error;
    bool finished = false;
    D downstream;

  public:
    explicit inject_error(int events_before_error, D &&downstream)
        : events_to_emit_before_error(events_before_error),
          downstream(std::move(downstream)) {}

    template <typename E> void handle_event(E const &event) noexcept {
        if (events_to_emit_before_error-- > 0) {
            downstream.handle_event(event);
        } else {
            downstream.handle_end(
                std::make_exception_ptr(std::runtime_error("injected error")));
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (events_to_emit_before_error > 0)
            downstream.handle_end(error);
    }
};

auto make_merge_fixture_error_on_input0(macrotime max_shift,
                                        int events_before_error) {
    auto make_proc = [max_shift, events_before_error](auto &&downstream) {
        auto [input0, input1] =
            merge<test_events_0123>(max_shift, std::move(downstream));
        auto error0 = inject_error(events_before_error, std::move(input0));
        return split_events<test_events_23, decltype(error0),
                            decltype(input1)>(std::move(error0),
                                              std::move(input1));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        make_proc);
}

auto make_merge_fixture_error_on_input1(macrotime max_shift,
                                        int events_before_error) {
    auto make_proc = [max_shift, events_before_error](auto &&downstream) {
        auto [input0, input1] =
            merge<test_events_0123>(max_shift, std::move(downstream));
        auto error1 = inject_error(events_before_error, std::move(input1));
        return split_events<test_events_23, decltype(input0),
                            decltype(error1)>(std::move(input0),
                                              std::move(error1));
    };

    return make_processor_test_fixture<test_events_0123, test_events_0123>(
        make_proc);
}

using out_vec = std::vector<event_variant<test_events_0123>>;

TEST_CASE("Merge with error on one input", "[merge]") {
    SECTION("Input0 error with no events pending") {
        bool further_input_on_input1 = GENERATE(false, true);
        bool end_input1 = GENERATE(false, true);

        auto f = make_merge_fixture_error_on_input0(1000, 0);
        f.feed_events({
            test_event<0>{0}, // Fixture will convert to error
        });
        REQUIRE(f.output() == out_vec{});
        if (further_input_on_input1) {
            f.feed_events({
                test_event<2>{1},
            });
            REQUIRE(f.output() == out_vec{});
        }
        if (end_input1) {
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
        }
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("injected error"));
    }

    SECTION("Input1 error with no events pending") {
        bool further_input_on_input0 = GENERATE(false, true);
        bool end_input0 = GENERATE(false, true);

        auto f = make_merge_fixture_error_on_input1(1000, 0);
        f.feed_events({
            test_event<2>{0}, // Fixture will convert to error
        });
        REQUIRE(f.output() == out_vec{});
        if (further_input_on_input0) {
            f.feed_events({
                test_event<0>{1}, // Further input ignored on other input
            });
            REQUIRE(f.output() == out_vec{});
        }
        if (end_input0) {
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
        }
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("injected error"));
    }

    SECTION("Input0 error with input0 events pending") {
        bool end_input1 = GENERATE(false, true);

        auto f = make_merge_fixture_error_on_input0(1000, 1);
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<0>{1}, // Fixture will convert to error
        });
        REQUIRE(f.output() == out_vec{});
        if (end_input1) {
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
        }
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("injected error"));
    }

    SECTION("Input0 error with input1 events pending") {
        bool end_input1 = GENERATE(false, true);

        auto f = make_merge_fixture_error_on_input0(1000, 0);
        f.feed_events({
            test_event<2>{0},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<0>{1}, // Fixture will convert to error
        });
        REQUIRE(f.output() == out_vec{});
        if (end_input1) {
            f.feed_end({});
            REQUIRE(f.output() == out_vec{});
        }
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("injected error"));
    }
}

TEST_CASE("Merge", "[merge]") {
    auto f = make_merge_fixture(1000);

    SECTION("Empty streams yield empty stream") {
        f.feed_end({});
        REQUIRE(f.output() == out_vec{});
    }

    SECTION("Errors on both inputs") {
        f.feed_end(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f.output() == out_vec{});
        REQUIRE_THROWS_MATCHES(f.did_end(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Input0 events are emitted before input1 events") {
        f.feed_events({
            test_event<2>{42},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<0>{42},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{42},
                              });
        f.feed_events({
            test_event<3>{42},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<1>{42},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<1>{42},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{
                                  test_event<2>{42},
                                  test_event<3>{42},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Already sorted in macrotime order") {
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == out_vec{test_event<0>{1}});
        f.feed_events({
            test_event<0>{3},
        });
        REQUIRE(f.output() == out_vec{test_event<2>{2}});
        f.feed_end({});
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{3},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Delayed input0 sorted by macrotime") {
        f.feed_events({
            test_event<0>{2},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<2>{1},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<2>{1},
                              });
        f.feed_events({
            test_event<0>{4},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<2>{3},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{2},
                                  test_event<2>{3},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{4},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Delayed input1 sorted by macrotime") {
        f.feed_events({
            test_event<2>{2},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<0>{1},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{1},
                              });
        f.feed_events({
            test_event<2>{4},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<0>{3},
        });
        REQUIRE(f.output() == out_vec{
                                  test_event<2>{2},
                                  test_event<0>{3},
                              });
        f.feed_end({});
        REQUIRE(f.output() == out_vec{
                                  test_event<2>{4},
                              });
        REQUIRE(f.did_end());
    }
}

TEST_CASE("Merge max time shift", "[merge]") {
    auto f = make_merge_fixture(10);

    SECTION("Input0 emitted after exceeding max time shift") {
        f.feed_events({
            test_event<0>{0},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<0>{10},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<0>{11},
        });
        REQUIRE(f.output() == out_vec{test_event<0>{0}});
        f.feed_end({});
        REQUIRE(f.output() == out_vec{
                                  test_event<0>{10},
                                  test_event<0>{11},
                              });
        REQUIRE(f.did_end());
    }

    SECTION("Input1 emitted after exceeding max time shift") {
        f.feed_events({
            test_event<1>{0},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<1>{10},
        });
        REQUIRE(f.output() == out_vec{});
        f.feed_events({
            test_event<1>{11},
        });
        REQUIRE(f.output() == out_vec{test_event<1>{0}});
        f.feed_end({});
        REQUIRE(f.output() == out_vec{
                                  test_event<1>{10},
                                  test_event<1>{11},
                              });
        REQUIRE(f.did_end());
    }
}
