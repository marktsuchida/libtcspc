/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/split.hpp"

#include "libtcspc/common.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

namespace tcspc {

using e0 = empty_test_event<0>;
using e1 = empty_test_event<1>;
using e2 = empty_test_event<2>;
using e3 = empty_test_event<3>;

TEST_CASE("Split events", "[split]") {
    auto out0 = capture_output<event_set<e0, e1>>();
    auto out1 = capture_output<event_set<e2, e3>>();
    auto in = feed_input<event_set<e0, e1, e2, e3>>(
        split<event_set<e2, e3>>(ref_processor(out0), ref_processor(out1)));
    in.require_output_checked(out0);
    in.require_output_checked(out1);

    SECTION("Empty stream yields empty streams") {
        in.flush();
        REQUIRE(out0.check_flushed());
        REQUIRE(out1.check_flushed());
    }

    SECTION("Events are split") {
        in.feed(e0{});
        REQUIRE(out0.check(e0{}));
        in.feed(e2{});
        REQUIRE(out1.check(e2{}));
        in.flush();
        REQUIRE(out0.check_flushed());
        REQUIRE(out1.check_flushed());
    }

    SECTION("Error on out0 event propagates without flushing out1") {
        out0.throw_error_on_next();
        REQUIRE_THROWS(in.feed(e0{}));
        REQUIRE(out1.check_not_flushed());
    }

    SECTION("Error on out1 event propagates without flushing out0") {
        out1.throw_error_on_next();
        REQUIRE_THROWS(in.feed(e2{}));
        REQUIRE(out0.check_not_flushed());
    }

    SECTION("End on out0 event propagates, flushing out1") {
        SECTION("Out1 not throwing") {
            out0.throw_end_processing_on_next();
            REQUIRE_THROWS_AS(in.feed(e0{}), end_processing);
            REQUIRE(out1.check_flushed());
        }

        SECTION("Out1 ending on flush") {
            out0.throw_end_processing_on_next();
            out1.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.feed(e0{}), end_processing);
            REQUIRE(out1.check_flushed());
        }

        SECTION("Out1 throwing error on flush") {
            out0.throw_end_processing_on_next();
            out1.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.feed(e0{}), std::runtime_error);
        }
    }

    SECTION("End on out1 event propagates, flushing out0") {
        SECTION("Out0 not throwing") {
            out1.throw_end_processing_on_next();
            REQUIRE_THROWS_AS(in.feed(e2{}), end_processing);
            REQUIRE(out0.check_flushed());
        }

        SECTION("Out0 ending on flush") {
            out1.throw_end_processing_on_next();
            out0.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.feed(e2{}), end_processing);
            REQUIRE(out0.check_flushed());
        }

        SECTION("Out0 throwing error on flush") {
            out1.throw_end_processing_on_next();
            out0.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.feed(e2{}), std::runtime_error);
        }
    }

    SECTION("Error on out0 flush propagates without flushing out1") {
        out0.throw_error_on_flush();
        REQUIRE_THROWS(in.flush());
        REQUIRE(out1.check_not_flushed());
    }

    SECTION("Error on out1 flush propagates without flushing out0") {
        out1.throw_error_on_flush();
        REQUIRE_THROWS(in.flush());
        // out0 would have been flushed before out1 threw the error (lack of
        // double-flush is checked by capture_output)
        REQUIRE(out0.check_flushed());
    }

    SECTION("End on out0 flush propagates, flushing out1") {
        SECTION("Out1 not throwing") {
            out0.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.flush(), end_processing);
            REQUIRE(out0.check_flushed());
            REQUIRE(out1.check_flushed());
        }

        SECTION("Out1 ending on flush") {
            out0.throw_end_processing_on_flush();
            out1.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.flush(), end_processing);
        }

        SECTION("Out1 throwing error on flush") {
            out0.throw_end_processing_on_flush();
            out1.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.flush(), std::runtime_error);
        }
    }

    SECTION("End on out1 flush propagates, flushing out0") {
        SECTION("Out0 not throwing") {
            out1.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.flush(), end_processing);
            REQUIRE(out0.check_flushed());
            REQUIRE(out1.check_flushed());
        }

        SECTION("Out0 ending on flush") {
            out1.throw_end_processing_on_flush();
            out0.throw_end_processing_on_flush();
            REQUIRE_THROWS_AS(in.flush(), end_processing);
        }

        SECTION("Out0 throwing error on flush") {
            out1.throw_end_processing_on_flush();
            out0.throw_error_on_flush();
            REQUIRE_THROWS_AS(in.flush(), std::runtime_error);
        }
    }
}

TEST_CASE("Split events, empty on out0", "[split]") {
    auto out0 = capture_output<event_set<>>();
    auto out1 = capture_output<event_set<e0>>();
    auto in = feed_input<event_set<e0>>(
        split<event_set<e0>>(ref_processor(out0), ref_processor(out1)));
    in.require_output_checked(out0);
    in.require_output_checked(out1);

    in.feed(e0{});
    REQUIRE(out1.check(e0{}));
    in.flush();
    REQUIRE(out0.check_flushed());
    REQUIRE(out1.check_flushed());
}

TEST_CASE("Split events, empty on out1", "[split]") {
    auto out0 = capture_output<event_set<e0>>();
    auto out1 = capture_output<event_set<>>();
    auto in = feed_input<event_set<e0>>(
        split<event_set<>>(ref_processor(out0), ref_processor(out1)));
    in.require_output_checked(out0);
    in.require_output_checked(out1);

    in.feed(e0{});
    REQUIRE(out0.check(e0{}));
    in.flush();
    REQUIRE(out0.check_flushed());
    REQUIRE(out1.check_flushed());
}

} // namespace tcspc
