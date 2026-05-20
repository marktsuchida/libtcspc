/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/event.hpp"

#include "libtcspc/core.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"

#include <catch2/catch_test_macros.hpp>

#include <ostream>

namespace tcspc::internal {

TEST_CASE("abstime_stamped") {
    struct have_not {};
    struct have {
        int abstime;
    };
    STATIC_CHECK_FALSE(abstime_stamped<have_not>);
    STATIC_CHECK(abstime_stamped<have>);
}

} // namespace tcspc::internal

namespace tcspc::tests {

struct comparable_and_ostreamable {
    int x = 0;
    friend auto operator==(comparable_and_ostreamable const &,
                           comparable_and_ostreamable const &)
        -> bool = default;
    friend auto operator<<(std::ostream &s,
                           comparable_and_ostreamable const & /*unused*/)
        -> std::ostream & {
        return s << "comparable_and_ostreamable";
    }
};

struct comparable_only {
    int x = 0;
    friend auto operator==(comparable_only const &, comparable_only const &)
        -> bool = default;
};

struct ostreamable_only {
    int x = 0;
    friend auto operator<<(std::ostream &s,
                           ostreamable_only const & /*unused*/)
        -> std::ostream & {
        return s;
    }
};

struct plain {
    int x = 0;
};

TEST_CASE("internal ostreamable concept") {
    STATIC_CHECK(internal::ostreamable<comparable_and_ostreamable>);
    STATIC_CHECK_FALSE(internal::ostreamable<comparable_only>);
    STATIC_CHECK(internal::ostreamable<ostreamable_only>);
    STATIC_CHECK_FALSE(internal::ostreamable<plain>);

    STATIC_CHECK(internal::ostreamable<warning_event>);
    STATIC_CHECK(internal::ostreamable<time_reached_event<>>);
}

TEST_CASE("is_ostreamable_list_v") {
    STATIC_CHECK_FALSE(internal::is_ostreamable_list_v<int>);
    STATIC_CHECK(internal::is_ostreamable_list_v<type_list<>>);
    STATIC_CHECK(internal::is_ostreamable_list_v<
                 type_list<comparable_and_ostreamable, ostreamable_only>>);
    STATIC_CHECK_FALSE(
        internal::is_ostreamable_list_v<type_list<comparable_only>>);
    STATIC_CHECK_FALSE(
        internal::is_ostreamable_list_v<
            type_list<comparable_and_ostreamable, comparable_only>>);
}

} // namespace tcspc::tests
