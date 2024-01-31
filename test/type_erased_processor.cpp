/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/type_erased_processor.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;

static_assert(
    handles_event_set_v<type_erased_processor<event_set<>>, event_set<>>);
static_assert(not handles_event_set_v<type_erased_processor<event_set<>>,
                                      event_set<e0>>);
static_assert(
    handles_event_set_v<type_erased_processor<event_set<e0>>, event_set<e0>>);
static_assert(handles_event_set_v<type_erased_processor<event_set<e0, e1>>,
                                  event_set<e0, e1>>);

// handles_event_set_v works even if the functions are virtual.
static_assert(
    handles_event_set_v<internal::abstract_processor_impl<>, event_set<>>);
static_assert(not handles_event_set_v<internal::abstract_processor_impl<>,
                                      event_set<e0>>);
static_assert(
    handles_event_set_v<internal::abstract_processor_impl<e0>, event_set<e0>>);
static_assert(handles_event_set_v<internal::abstract_processor_impl<e0, e1>,
                                  event_set<e0, e1>>);

static_assert(handles_event_set_v<
              internal::virtual_processor<event_set_sink<event_set<>>>,
              event_set<>>);
static_assert(not handles_event_set_v<
              internal::virtual_processor<event_set_sink<event_set<>>>,
              event_set<e0>>);
static_assert(handles_event_set_v<
              internal::virtual_processor<event_set_sink<event_set<e0>>, e0>,
              event_set<e0>>);
static_assert(
    handles_event_set_v<
        internal::virtual_processor<event_set_sink<event_set<e0, e1>>, e0, e1>,
        event_set<e0, e1>>);

TEST_CASE("type_erased_processor move assignment") {
    // Create with stub downstream.
    type_erased_processor<event_set<e0>> tep;

    struct myproc {
        static void handle(e0 const &event) { (void)event; }
        static void flush() {}
    };

    tep = decltype(tep)(myproc());
}

} // namespace tcspc
