/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "flimevt/histogram_array.hpp"

#include "flimevt/event_set.hpp"
#include "flimevt/ref_processor.hpp"
#include "flimevt/test_utils.hpp"

#include <cstdint>

#include <catch2/catch.hpp>

using namespace flimevt;

using u16 = std::uint16_t;
using start_event = timestamped_test_event<0>;
using misc_event = timestamped_test_event<1>;

TEST_CASE("Journal bin increment batches", "[journal_bin_increment_batches]") {
    auto out = capture_output<
        event_set<bin_increment_batch_event<u16>, start_event, misc_event,
                  bin_increment_batch_journal_event<u16>,
                  partial_bin_increment_batch_journal_event<u16>>>();
    auto in = feed_input<
        event_set<bin_increment_batch_event<u16>, start_event, misc_event>>(
        journal_bin_increment_batches<u16, start_event>(1,
                                                        ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Pass through unrelated") {
        in.feed(misc_event{42});
        REQUIRE(out.check(misc_event{42}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Ignore before start") {
        in.feed(bin_increment_batch_event<u16>{42, 43, {}});
        in.feed(bin_increment_batch_event<u16>{44, 45, {123, 456}});
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Normal operation") {
        bin_increment_batch_journal<u16> expected_journal;

        in.feed(start_event{42});
        REQUIRE(out.check(start_event{42}));
        in.feed(bin_increment_batch_event<u16>{43, 44, {123, 456}});
        REQUIRE(out.check(bin_increment_batch_event<u16>{43, 44, {123, 456}}));
        expected_journal.append_batch({123, 456});
        REQUIRE(out.check(
            bin_increment_batch_journal_event<u16>{43, 44, expected_journal}));

        in.feed(start_event{45});
        REQUIRE(out.check(start_event{45}));
        in.feed(bin_increment_batch_event<u16>{46, 47, {789}});
        REQUIRE(out.check(bin_increment_batch_event<u16>{46, 47, {789}}));
        expected_journal.clear();
        expected_journal.append_batch({789});
        REQUIRE(out.check(
            bin_increment_batch_journal_event<u16>{46, 47, expected_journal}));

        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Ignore extra batches") {
        bin_increment_batch_journal<u16> expected_journal;

        in.feed(start_event{42});
        REQUIRE(out.check(start_event{42}));
        in.feed(bin_increment_batch_event<u16>{43, 44, {123, 456}});
        REQUIRE(out.check(bin_increment_batch_event<u16>{43, 44, {123, 456}}));
        expected_journal.append_batch({123, 456});
        REQUIRE(out.check(
            bin_increment_batch_journal_event<u16>{43, 44, expected_journal}));

        in.feed(bin_increment_batch_event<u16>{45, 46, {789}});
        in.feed(start_event{47});
        REQUIRE(out.check(start_event{47}));
        in.feed(bin_increment_batch_event<u16>{48, 49, {234}});
        REQUIRE(out.check(bin_increment_batch_event<u16>{48, 49, {234}}));
        expected_journal.clear();
        expected_journal.append_batch({234});
        REQUIRE(out.check(
            bin_increment_batch_journal_event<u16>{48, 49, expected_journal}));
    }

    SECTION("Emit partial cycle") {
        in.feed(start_event{42});
        REQUIRE(out.check(start_event{42}));
        in.feed(start_event{43});
        REQUIRE(out.check(
            partial_bin_increment_batch_journal_event<u16>{0, 0, {}}));
        REQUIRE(out.check(start_event{43}));
        in.feed_end();
        REQUIRE(out.check(
            partial_bin_increment_batch_journal_event<u16>{0, 0, {}}));
        REQUIRE(out.check_end());
    }
}
