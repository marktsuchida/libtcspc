/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/type_erased_processor.hpp"

#include "libtcspc/discard.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

using namespace tcspc;

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

static_assert(
    handles_event_set_v<type_erased_processor<event_set<>>, event_set<>>);
static_assert(
    !handles_event_set_v<type_erased_processor<event_set<>>, event_set<e0>>);
static_assert(
    handles_event_set_v<type_erased_processor<event_set<e0>>, event_set<e0>>);
static_assert(handles_event_set_v<type_erased_processor<event_set<e0, e1>>,
                                  event_set<e0, e1>>);

// handles_event_set_v works even if the functions are virtual.
static_assert(
    handles_event_set_v<internal::abstract_processor_impl<>, event_set<>>);
static_assert(
    !handles_event_set_v<internal::abstract_processor_impl<>, event_set<e0>>);
static_assert(
    handles_event_set_v<internal::abstract_processor_impl<e0>, event_set<e0>>);
static_assert(handles_event_set_v<internal::abstract_processor_impl<e0, e1>,
                                  event_set<e0, e1>>);

static_assert(
    handles_event_set_v<internal::virtual_processor<discard_all<event_set<>>>,
                        event_set<>>);
static_assert(
    !handles_event_set_v<internal::virtual_processor<discard_all<event_set<>>>,
                         event_set<e0>>);
static_assert(handles_event_set_v<
              internal::virtual_processor<discard_all<event_set<e0>>, e0>,
              event_set<e0>>);
static_assert(
    handles_event_set_v<
        internal::virtual_processor<discard_all<event_set<e0, e1>>, e0, e1>,
        event_set<e0, e1>>);

TEST_CASE("type_erased_processor move assignment", "[type_erased_processor]") {
    // Create with stub downstream.
    type_erased_processor<event_set<e0>> tep;

    struct myproc {
        static void handle_event(e0 const &event) noexcept { (void)event; }
        static void handle_end(std::exception_ptr const &error) noexcept {
            (void)error;
        }
    };

    tep = decltype(tep)(myproc());
}
