/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/data_types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace tcspc {

TEST_CASE("data types concepts on default_data_types") {
    STATIC_CHECK(with_abstime_type<default_data_types>);
    STATIC_CHECK(with_channel_type<default_data_types>);
    STATIC_CHECK(with_difftime_type<default_data_types>);
    STATIC_CHECK(with_count_type<default_data_types>);
    STATIC_CHECK(with_datapoint_type<default_data_types>);
    STATIC_CHECK(with_bin_index_type<default_data_types>);
    STATIC_CHECK(with_bin_type<default_data_types>);
}

TEST_CASE("data types concepts fail on non-integral member") {
    struct abstime_string {
        std::string abstime;
    };
    struct abstime_ptr {
        char *abstime;
    };
    STATIC_CHECK_FALSE(with_abstime_type<abstime_string>);
    STATIC_CHECK_FALSE(with_abstime_type<abstime_ptr>);
}

} // namespace tcspc
