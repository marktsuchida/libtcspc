/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/own_on_copy_view.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <utility>
#include <vector>

namespace tcspc {

TEST_CASE("Autocopying span") {
    std::array a{1, 2, 3};
    own_on_copy_view const aspan(a); // CTAD check

    std::vector v{1, 2, 3};

    own_on_copy_view arr(v);
    // Can mutate if T is non-const
    ++arr.as_span()[0];

    own_on_copy_view<int const> carr(v);
    // Shares the underlying memory of v
    REQUIRE(carr.as_span()[0] == 2);

    auto arr_copy = arr;
    ++arr_copy.as_span()[0];
    // Copy does not share memory
    REQUIRE(carr.as_span()[0] == 2);

    auto arr_moved = std::move(arr);
    ++arr_moved.as_span()[0];
    // Moved view points to original memory
    REQUIRE(carr.as_span()[0] == 3);

    own_on_copy_view<int> empty;
    REQUIRE(empty.as_span().empty());

    // Copying an empty instance allocates T[0] but should just work
    own_on_copy_view empty_copy(empty);
    REQUIRE(empty_copy.as_span().empty());

    // Copying const to const
    auto carr_copy = carr;

    // Make sure we're not getting lucky with small indices
    std::vector big(4096, 42);
    own_on_copy_view big_view(big);
    REQUIRE(big_view.as_span()[4095] == 42);
    auto big_copy(big_view);
    REQUIRE(big_copy.as_span()[4095] == 42);
}

} // namespace tcspc
