/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/autocopy_span.hpp"

#include <array>
#include <utility>
#include <vector>

#include <catch2/catch_all.hpp>

namespace tcspc {

TEST_CASE("Autocopying span", "[autocopy_span]") {
    std::array a{1, 2, 3};
    autocopy_span const aspan(a); // CTAD check

    std::vector v{1, 2, 3};

    autocopy_span arr(v);
    // Can mutate if T is non-const
    ++arr.as_span()[0];

    autocopy_span<int const> carr(v);
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

    autocopy_span<int> empty;
    REQUIRE(empty.as_span().empty());

    // Copying an empty instance allocates T[0] but should just work
    autocopy_span empty_copy(empty);
    REQUIRE(empty_copy.as_span().empty());

    // Conversion to span
    span<int const> const s(carr);
    REQUIRE(s[0] == 3);

    // Implicit conversion to span
    auto lamb = [](span<int const> s) { return s[0]; };
    REQUIRE(lamb(carr) == 3);

    // Copying const to const
    auto carr_copy = carr;

    // Make sure we're not getting lucky with small indices
    std::vector big(4096, 42);
    autocopy_span big_view(big);
    REQUIRE(big_view.as_span()[4095] == 42);
    auto big_copy(big_view);
    REQUIRE(big_copy.as_span()[4095] == 42);
}

} // namespace tcspc
