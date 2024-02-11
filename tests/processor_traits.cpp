/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/processor_traits.hpp"

#include "libtcspc/type_list.hpp"

#include <catch2/catch_test_macros.hpp>

namespace tcspc {

TEST_CASE("handles_flush") {
    struct No1 {};
    struct No2 {
        auto flush() -> int;
    };
    struct No3 {
        void flush(int);
    };
    struct Yes {
        void flush();
    };
    static_assert(not handles_flush_v<No1>);
    static_assert(not handles_flush_v<No2>);
    static_assert(not handles_flush_v<No3>);
    static_assert(handles_flush_v<Yes>);
}

TEST_CASE("handles_event") {
    struct Something {};
    struct No1 {};
    struct No2 {
        auto handle(int) -> int;
    };
    struct No3 {
        void handle(Something const &);
    };
    struct Yes {
        void handle(int);
    };
    static_assert(not handles_event_v<No1, int>);
    static_assert(not handles_event_v<No2, int>);
    static_assert(not handles_event_v<No3, int>);
    static_assert(handles_event_v<Yes, int>);
}

TEST_CASE("handles_events") {
    struct E0 {};
    struct E1 {};
    struct E2 {};
    struct No1 {};
    struct No2 {
        void handle(E2 const &);
    };
    struct No3 {
        void handle(E0 const &);
    };
    struct Yes0 {
        void handle(E0 const &);
        void handle(E1 const &);
    };
    struct Yes1 {
        void handle(E0 const &);
        void handle(E1 const &);
        void handle(E2 const &);
    };
    static_assert(not handles_events_v<No1, type_list<E0, E1>>);
    static_assert(not handles_events_v<No2, type_list<E0, E1>>);
    static_assert(not handles_events_v<No3, type_list<E0, E1>>);
    static_assert(handles_events_v<Yes0, type_list<E0, E1>>);
    static_assert(handles_events_v<Yes1, type_list<E0, E1>>);
}

} // namespace tcspc
