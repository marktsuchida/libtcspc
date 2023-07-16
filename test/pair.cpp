/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/pair.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

TEST_CASE("pair all", "[pair_all]") {
    auto out =
        capture_output<event_set<detection_event<>, detection_pair_event<>>>();
    auto in =
        feed_input<event_set<detection_event<>>>(pair_all<default_data_traits>(
            0, std::array<default_data_traits::channel_type, 1>{1}, 2,
            ref_processor(out)));
    in.require_output_checked(out);

    SECTION("empty stream") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{0}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));

        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));
        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));
        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_pair_event<>{{{{3}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));

        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));

        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));
        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));
        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));

        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("pair all with self", "[pair_all]") {
    auto out =
        capture_output<event_set<detection_event<>, detection_pair_event<>>>();
    auto in =
        feed_input<event_set<detection_event<>>>(pair_all<default_data_traits>(
            0, std::array<default_data_traits::channel_type, 1>{0}, 2,
            ref_processor(out)));
    in.require_output_checked(out);

    in.feed(detection_event<>{{{0}, 0}});
    REQUIRE(out.check(detection_event<>{{{0}, 0}}));

    in.feed(detection_event<>{{{1}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{1}, 0}}));

    in.feed(detection_event<>{{{2}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{2}, 0}}));

    in.feed(detection_event<>{{{3}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_pair_event<>{{{{2}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{3}, 0}}));

    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("pair one", "[pair_one]") {
    auto out =
        capture_output<event_set<detection_event<>, detection_pair_event<>>>();
    auto in =
        feed_input<event_set<detection_event<>>>(pair_one<default_data_traits>(
            0, std::array<default_data_traits::channel_type, 1>{1}, 2,
            ref_processor(out)));
    in.require_output_checked(out);

    SECTION("empty stream") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{0}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));

        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));
        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));
        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{3}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));

        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));

        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));
        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));
        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));

        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("pair one with self", "[pair_one]") {
    auto out =
        capture_output<event_set<detection_event<>, detection_pair_event<>>>();
    auto in =
        feed_input<event_set<detection_event<>>>(pair_one<default_data_traits>(
            0, std::array<default_data_traits::channel_type, 1>{0}, 2,
            ref_processor(out)));
    in.require_output_checked(out);

    in.feed(detection_event<>{{{0}, 0}});
    REQUIRE(out.check(detection_event<>{{{0}, 0}}));

    in.feed(detection_event<>{{{1}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{1}, 0}}));

    in.feed(detection_event<>{{{2}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{2}, 0}}));

    in.feed(detection_event<>{{{3}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{2}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{3}, 0}}));

    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("pair all between", "[pair_all_between]") {
    auto out =
        capture_output<event_set<detection_event<>, detection_pair_event<>>>();
    auto in = feed_input<event_set<detection_event<>>>(
        pair_all_between<default_data_traits>(
            0, std::array<default_data_traits::channel_type, 1>{1}, 2,
            ref_processor(out)));
    in.require_output_checked(out);

    SECTION("empty stream") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{0}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));

        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));
        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));
        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{3}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));

        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));

        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));
        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));
        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));

        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("pair all between with self", "[pair_all_between]") {
    auto out =
        capture_output<event_set<detection_event<>, detection_pair_event<>>>();
    auto in = feed_input<event_set<detection_event<>>>(
        pair_all_between<default_data_traits>(
            0, std::array<default_data_traits::channel_type, 1>{0}, 2,
            ref_processor(out)));
    in.require_output_checked(out);

    in.feed(detection_event<>{{{0}, 0}});
    REQUIRE(out.check(detection_event<>{{{0}, 0}}));

    in.feed(detection_event<>{{{1}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{1}, 0}}));

    in.feed(detection_event<>{{{2}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{2}, 0}}));

    in.feed(detection_event<>{{{3}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{2}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{3}, 0}}));

    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("pair one between", "[pair_one_between]") {
    auto out =
        capture_output<event_set<detection_event<>, detection_pair_event<>>>();
    auto in = feed_input<event_set<detection_event<>>>(
        pair_one_between<default_data_traits>(
            0, std::array<default_data_traits::channel_type, 1>{1}, 2,
            ref_processor(out)));
    in.require_output_checked(out);

    SECTION("empty stream") {
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("stops following starts") {
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{0}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));

        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));
        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));
        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{3}, 0}}, {{{3}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));

        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("starts following stops") {
        in.feed(detection_event<>{{{0}, 1}});
        REQUIRE(out.check(detection_event<>{{{0}, 1}}));
        in.feed(detection_event<>{{{0}, 0}});
        REQUIRE(out.check(detection_event<>{{{0}, 0}}));

        in.feed(detection_event<>{{{1}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{1}, 1}}));
        in.feed(detection_event<>{{{1}, 0}});
        REQUIRE(out.check(detection_event<>{{{1}, 0}}));

        in.feed(detection_event<>{{{2}, 1}});
        REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 1}}}));
        REQUIRE(out.check(detection_event<>{{{2}, 1}}));

        in.feed(detection_event<>{{{3}, 1}});
        REQUIRE(out.check(detection_event<>{{{3}, 1}}));
        in.feed(detection_event<>{{{3}, 0}});
        REQUIRE(out.check(detection_event<>{{{3}, 0}}));

        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("pair one between with self", "[pair_one_between]") {
    auto out =
        capture_output<event_set<detection_event<>, detection_pair_event<>>>();
    auto in = feed_input<event_set<detection_event<>>>(
        pair_one_between<default_data_traits>(
            0, std::array<default_data_traits::channel_type, 1>{0}, 2,
            ref_processor(out)));
    in.require_output_checked(out);

    in.feed(detection_event<>{{{0}, 0}});
    REQUIRE(out.check(detection_event<>{{{0}, 0}}));

    in.feed(detection_event<>{{{1}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{0}, 0}}, {{{1}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{1}, 0}}));

    in.feed(detection_event<>{{{2}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{1}, 0}}, {{{2}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{2}, 0}}));

    in.feed(detection_event<>{{{3}, 0}});
    REQUIRE(out.check(detection_pair_event<>{{{{2}, 0}}, {{{3}, 0}}}));
    REQUIRE(out.check(detection_event<>{{{3}, 0}}));

    in.feed_end();
    REQUIRE(out.check_end());
}

} // namespace tcspc
