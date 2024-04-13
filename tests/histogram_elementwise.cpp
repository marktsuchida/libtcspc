/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/histogram_elementwise.hpp"

#include "libtcspc/bucket.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/errors.hpp"
#include "libtcspc/histogram_events.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>

namespace tcspc {

namespace {

using start_event = timestamped_test_event<0>;
using reset_event = timestamped_test_event<2>;
using misc_event = timestamped_test_event<3>;

struct dt3216 : default_data_traits {
    using bin_index_type = u32;
    using bin_type = u16;
};

struct dt88 : default_data_traits {
    using bin_index_type = u8;
    using bin_type = u8;
};

template <typename T> auto tmp_bucket(T &v) {
    struct ignore_storage {};
    return bucket(span(v), ignore_storage{});
}

template <typename DT>
auto make_bin_increment_batch(
    std::initializer_list<typename DT::bin_index_type> il) {
    auto s = new_delete_bucket_source<typename DT::bin_index_type>::create();
    auto b = s->bucket_of_size(il.size());
    std::copy(il.begin(), il.end(), b.begin());
    return bin_increment_batch_event<DT>{b};
}

} // namespace

TEST_CASE("introspect histogram_elementwise", "[introspect]") {
    check_introspect_simple_processor(
        histogram_elementwise<saturate_on_overflow>(
            1, 1, 255, new_delete_bucket_source<u16>::create(), null_sink()));
    check_introspect_simple_processor(
        histogram_elementwise_accumulate<reset_event, saturate_on_overflow>(
            1, 1, 255, new_delete_bucket_source<u16>::create(), null_sink()));
}

//
// Test cases for histogram_elementwise
//

TEMPLATE_TEST_CASE("Histogram elementwise, no overflow",
                   "[histogram_elementwise]", saturate_on_overflow,
                   error_on_overflow) {
    using out_events = type_list<element_histogram_event<dt3216>,
                                 histogram_array_event<dt3216>, warning_event>;
    auto ctx = processor_context::create();
    auto in = feed_input<type_list<bin_increment_batch_event<dt3216>>>(
        histogram_elementwise<TestType, dt3216>(
            2, 2, 100, new_delete_bucket_source<u16>::create(),
            capture_output<out_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(
        ctx->access<capture_output_access>("out"));

    in.feed(bin_increment_batch_event<dt3216>{{0}});
    std::vector<u16> elem_hist{1, 0};
    REQUIRE(out.check(element_histogram_event<dt3216>{tmp_bucket(elem_hist)}));

    in.feed(bin_increment_batch_event<dt3216>{{0, 1}});
    elem_hist = {1, 1};
    REQUIRE(out.check(element_histogram_event<dt3216>{tmp_bucket(elem_hist)}));
    std::vector<u16> hist_arr{1, 0, 1, 1};
    REQUIRE(out.check(histogram_array_event<dt3216>{tmp_bucket(hist_arr)}));

    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("Histogram elementwise, saturate on overflow",
          "[histogram_elementwise]") {
    using out_events = type_list<element_histogram_event<dt3216>,
                                 histogram_array_event<dt3216>, warning_event>;
    auto ctx = processor_context::create();

    SECTION("Max per bin = 0") {
        auto in = feed_input<type_list<bin_increment_batch_event<dt3216>>>(
            histogram_elementwise<saturate_on_overflow, dt3216>(
                1, 1, 0, new_delete_bucket_source<u16>::create(),
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(bin_increment_batch_event<dt3216>{{0}}); // Overflow
        REQUIRE(out.check(warning_event{"histogram array saturated"}));
        std::vector<u16> elem_hist{0};
        REQUIRE(
            out.check(element_histogram_event<dt3216>{tmp_bucket(elem_hist)}));
        std::vector<u16> hist_arr{0};
        REQUIRE(
            out.check(histogram_array_event<dt3216>{tmp_bucket(hist_arr)}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<type_list<bin_increment_batch_event<dt3216>>>(
            histogram_elementwise<saturate_on_overflow, dt3216>(
                1, 1, 1, new_delete_bucket_source<u16>::create(),
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        in.feed(bin_increment_batch_event<dt3216>{{0, 0}}); // Overflow
        REQUIRE(out.check(warning_event{"histogram array saturated"}));
        std::vector<u16> elem_hist{1};
        REQUIRE(
            out.check(element_histogram_event<dt3216>{tmp_bucket(elem_hist)}));
        std::vector<u16> hist_arr{1};
        REQUIRE(
            out.check(histogram_array_event<dt3216>{tmp_bucket(hist_arr)}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Histogram elementwise, error on overflow",
          "[histogram_elementwise]") {
    using out_events = type_list<element_histogram_event<dt3216>,
                                 histogram_array_event<dt3216>>;
    auto ctx = processor_context::create();

    SECTION("Max per bin = 0") {
        auto in = feed_input<type_list<bin_increment_batch_event<dt3216>>>(
            histogram_elementwise<error_on_overflow, dt3216>(
                1, 1, 0, new_delete_bucket_source<u16>::create(),
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        REQUIRE_THROWS_AS(in.feed(bin_increment_batch_event<dt3216>{{0}}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("Max per bin = 1") {
        auto in = feed_input<type_list<bin_increment_batch_event<dt3216>>>(
            histogram_elementwise<error_on_overflow, dt3216>(
                1, 1, 1, new_delete_bucket_source<u16>::create(),
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(
            ctx->access<capture_output_access>("out"));

        REQUIRE_THROWS_AS(in.feed(bin_increment_batch_event<dt3216>{{0, 0}}),
                          histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }
}

//
// Test cases for histogram_elementwise_accumulate
//

// These are written in a newer style (per operation rather than per scenario)
// than above tests for histogram_elementwise (which should be updated).

namespace {

using hea_input_events =
    type_list<bin_increment_batch_event<dt88>, reset_event, misc_event>;
using hea_output_events =
    type_list<element_histogram_event<dt88>, histogram_array_event<dt88>,
              concluding_histogram_array_event<dt88>, misc_event>;

using hea_output_events_no_concluding =
    type_list<element_histogram_event<dt88>, histogram_array_event<dt88>,
              warning_event, misc_event>;

} // namespace

TEMPLATE_TEST_CASE(
    "histogram_elementwise_accumulate without emit-concluding yields empty stream from empty stream",
    "[histogram_elementwise_accumulate]", saturate_on_overflow,
    reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto num_elements = GENERATE(std::size_t(1), 3);
    auto num_bins = GENERATE(std::size_t(1), 4);
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, TestType, false, dt88>(
            num_elements, num_bins, 10, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events_no_concluding>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events_no_concluding>(
        ctx->access<capture_output_access>("out"));

    SECTION("empty stream") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("unrelated event only") {
        in.feed(misc_event{});
        REQUIRE(out.check(misc_event{}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEMPLATE_TEST_CASE(
    "histogram_elementwise_accumulate without emit-concluding finishes correctly",
    "[histogram_elementwise_accumulate]", saturate_on_overflow,
    reset_on_overflow, stop_on_overflow, error_on_overflow) {
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, TestType, false, dt88>(
            2, 3, 255, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events_no_concluding>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events_no_concluding>(
        ctx->access<capture_output_access>("out"));

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("end before cycle 0") {
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("feed cycle 0, element 0") {
        in.feed(bin_increment_batch_event<dt88>{{0}});
        elem_hist = {1, 0, 0};
        REQUIRE(
            out.check(element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

        SECTION("end mid cycle 0") {
            in.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("feed cycle 0, element 1") {
            in.feed(bin_increment_batch_event<dt88>{{1}});
            elem_hist = {0, 1, 0};
            REQUIRE(out.check(
                element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
            hist_arr = {1, 0, 0, 0, 1, 0};
            REQUIRE(
                out.check(histogram_array_event<dt88>{tmp_bucket(hist_arr)}));

            SECTION("end after cycle 0") {
                in.flush();
                REQUIRE(out.check_flushed());
            }

            SECTION("feed cycle 1, element 0") {
                in.feed(bin_increment_batch_event<dt88>{{2}});
                elem_hist = {1, 0, 1};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

                SECTION("end mid cycle 1") {
                    in.flush();
                    REQUIRE(out.check_flushed());
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE(
    "histogram_elementwise_accumulate with emit-concluding emits concluding event when ended",
    "[histogram_elementwise_accumulate]", reset_on_overflow, stop_on_overflow,
    error_on_overflow) {
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, TestType, true, dt88>(
            2, 3, 255, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events>(
        ctx->access<capture_output_access>("out"));

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("end before cycle 0") {
        in.flush();
        hist_arr = {0, 0, 0, 0, 0, 0};
        REQUIRE(out.check(
            concluding_histogram_array_event<dt88>{tmp_bucket(hist_arr)}));
        REQUIRE(out.check_flushed());
    }

    SECTION("feed cycle 0, element 0") {
        in.feed(bin_increment_batch_event<dt88>{{0}});
        elem_hist = {1, 0, 0};
        REQUIRE(
            out.check(element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

        SECTION("end mid cycle 0") {
            in.flush();
            hist_arr = {0, 0, 0, 0, 0, 0};
            REQUIRE(out.check(
                concluding_histogram_array_event<dt88>{tmp_bucket(hist_arr)}));
            REQUIRE(out.check_flushed());
        }

        SECTION("feed cycle 0, element 1") {
            in.feed(bin_increment_batch_event<dt88>{{1}});
            elem_hist = {0, 1, 0};
            REQUIRE(out.check(
                element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
            hist_arr = {1, 0, 0, 0, 1, 0};
            REQUIRE(
                out.check(histogram_array_event<dt88>{tmp_bucket(hist_arr)}));

            SECTION("end after cycle 0") {
                in.flush();
                hist_arr = {1, 0, 0, 0, 1, 0};
                REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                    tmp_bucket(hist_arr)}));
                REQUIRE(out.check_flushed());
            }

            SECTION("feed cycle 1, element 0") {
                in.feed(bin_increment_batch_event<dt88>{{2}});
                elem_hist = {1, 0, 1};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

                SECTION("end mid cycle 1") {
                    in.flush();
                    hist_arr = {1, 0, 0, 0, 1, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                        tmp_bucket(hist_arr)}));
                    REQUIRE(out.check_flushed());
                }
            }
        }
    }
}

TEMPLATE_TEST_CASE(
    "histogram_elementwise_accumulate with emit-concluding emits concluding event when reset",
    "[histogram_elementwise_accumulate]", reset_on_overflow, stop_on_overflow,
    error_on_overflow) {
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, TestType, true, dt88>(
            2, 3, 255, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events>(
        ctx->access<capture_output_access>("out"));

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("reset before cycle 0") {
        in.feed(reset_event{});
        hist_arr = {0, 0, 0, 0, 0, 0};
        REQUIRE(out.check(
            concluding_histogram_array_event<dt88>{tmp_bucket(hist_arr)}));
        in.flush();
        hist_arr = {0, 0, 0, 0, 0, 0};
        REQUIRE(out.check(
            concluding_histogram_array_event<dt88>{tmp_bucket(hist_arr)}));
        REQUIRE(out.check_flushed());
    }

    SECTION("feed cycle 0, element 0") {
        in.feed(bin_increment_batch_event<dt88>{{0}});
        elem_hist = {1, 0, 0};
        REQUIRE(
            out.check(element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

        SECTION("reset mid cycle 0") {
            in.feed(reset_event{});
            hist_arr = {0, 0, 0, 0, 0, 0};
            REQUIRE(out.check(
                concluding_histogram_array_event<dt88>{tmp_bucket(hist_arr)}));
            in.flush();
            hist_arr = {0, 0, 0, 0, 0, 0};
            REQUIRE(out.check(
                concluding_histogram_array_event<dt88>{tmp_bucket(hist_arr)}));
            REQUIRE(out.check_flushed());
        }

        SECTION("feed cycle 0, element 1") {
            in.feed(bin_increment_batch_event<dt88>{{1}});
            elem_hist = {0, 1, 0};
            REQUIRE(out.check(
                element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
            hist_arr = {1, 0, 0, 0, 1, 0};
            REQUIRE(
                out.check(histogram_array_event<dt88>{tmp_bucket(hist_arr)}));

            SECTION("reset after cycle 0") {
                in.feed(reset_event{});
                hist_arr = {1, 0, 0, 0, 1, 0};
                REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                    tmp_bucket(hist_arr)}));
                in.flush();
                hist_arr = {0, 0, 0, 0, 0, 0};
                REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                    tmp_bucket(hist_arr)}));
                REQUIRE(out.check_flushed());
            }

            SECTION("feed cycle 1, element 0") {
                in.feed(bin_increment_batch_event<dt88>{{2}});
                elem_hist = {1, 0, 1};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

                SECTION("reset mid cycle 1") {
                    in.feed(reset_event{});
                    hist_arr = {1, 0, 0, 0, 1, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                        tmp_bucket(hist_arr)}));
                    in.flush();
                    hist_arr = {0, 0, 0, 0, 0, 0};
                    REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                        tmp_bucket(hist_arr)}));
                    REQUIRE(out.check_flushed());
                }
            }
        }
    }
}

TEST_CASE("histogram_elementwise_accumulate with saturate-on-overflow",
          "[histogram_elementwise_accumulate]") {
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, saturate_on_overflow,
                                         false, dt88>(
            2, 3, 4, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events_no_concluding>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events_no_concluding>(
        ctx->access<capture_output_access>("out"));

    std::vector<u8> elem_hist;

    SECTION("overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<dt88>{{0, 0, 0, 0, 0, 0}});
        REQUIRE(out.check(warning_event{"histogram array saturated"}));
        elem_hist = {4, 0, 0};
        REQUIRE(
            out.check(element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

        SECTION("end") {
            in.flush();
            REQUIRE(out.check_flushed());
        }

        SECTION("reset") {
            in.feed(reset_event{});

            SECTION("saturate warned again after reset") {
                in.feed(bin_increment_batch_event<dt88>{{1, 1, 1, 1, 1}});
                REQUIRE(out.check(warning_event{"histogram array saturated"}));
                elem_hist = {0, 4, 0};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
                in.flush();
                REQUIRE(out.check_flushed());
            }
        }
    }
}

TEST_CASE("histogram_elementwise_accumulate with reset-on-overflow",
          "[histogram_elementwise_accumulate]") {
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, reset_on_overflow, true,
                                         dt88>(
            2, 3, 4, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events>(
        ctx->access<capture_output_access>("out"));

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("single-batch overflow during cycle 0, element 0") {
        REQUIRE_THROWS_AS(
            in.feed(bin_increment_batch_event<dt88>{{0, 0, 0, 0, 0, 0}}),
            histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("no overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<dt88>{{0, 0}});
        elem_hist = {2, 0, 0};
        REQUIRE(
            out.check(element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

        SECTION("single-batch overflow during cycle 0, element 1") {
            REQUIRE_THROWS_AS(
                in.feed(bin_increment_batch_event<dt88>{{1, 1, 1, 1, 1, 1}}),
                histogram_overflow_error);
            REQUIRE(out.check_not_flushed());
        }

        SECTION("no overflow during cycle 0, element 1") {
            in.feed(bin_increment_batch_event<dt88>{{1, 1}});
            elem_hist = {0, 2, 0};
            REQUIRE(out.check(
                element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
            hist_arr = {2, 0, 0, 0, 2, 0};
            REQUIRE(
                out.check(histogram_array_event<dt88>{tmp_bucket(hist_arr)}));

            SECTION("overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<dt88>{{0, 0, 0}});
                hist_arr = {2, 0, 0, 0, 2, 0};
                REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                    tmp_bucket(hist_arr)}));
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

                in.flush();
                hist_arr = {0, 0, 0, 0, 0, 0};
                REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                    tmp_bucket(hist_arr)}));
            }

            SECTION("single-batch overflow during cycle 1, element 0") {
                REQUIRE_THROWS_AS(in.feed(bin_increment_batch_event<dt88>{
                                      {0, 0, 0, 0, 0, 0}}),
                                  histogram_overflow_error);
                hist_arr = {2, 0, 0, 0, 2, 0};
                REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                    tmp_bucket(hist_arr)}));
                REQUIRE(out.check_not_flushed());
            }

            SECTION("no overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<dt88>{{0}});
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

                SECTION("overflow during cycle 1, element 1") {
                    in.feed(bin_increment_batch_event<dt88>{{1, 1, 1}});
                    hist_arr = {2, 0, 0, 0, 2, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                        tmp_bucket(hist_arr)}));
                    elem_hist = {0, 3, 0};
                    REQUIRE(out.check(
                        element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
                    hist_arr = {1, 0, 0, 0, 3, 0};
                    REQUIRE(out.check(
                        histogram_array_event<dt88>{tmp_bucket(hist_arr)}));

                    in.flush();
                    hist_arr = {1, 0, 0, 0, 3, 0};
                    REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                        tmp_bucket(hist_arr)}));
                }

                SECTION("single-batch overflow during cycle 1, element 1") {
                    REQUIRE_THROWS_AS(in.feed(bin_increment_batch_event<dt88>{
                                          {1, 1, 1, 1, 1, 1}}),
                                      histogram_overflow_error);
                    hist_arr = {2, 0, 0, 0, 2, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                        tmp_bucket(hist_arr)}));
                    REQUIRE(out.check_not_flushed());
                }
            }
        }
    }
}

TEST_CASE("histogram_elementwise_accumulate with stop-on-overflow",
          "[histogram_elementwise_accumulate]") {
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, stop_on_overflow, true,
                                         dt88>(
            2, 3, 4, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events>(
        ctx->access<capture_output_access>("out"));

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("overflow during cycle 0, element 0") {
        REQUIRE_THROWS_AS(
            in.feed(bin_increment_batch_event<dt88>{{0, 0, 0, 0, 0}}),
            end_processing);
        hist_arr = {0, 0, 0, 0, 0, 0};
        REQUIRE(out.check(
            concluding_histogram_array_event<dt88>{tmp_bucket(hist_arr)}));
        REQUIRE(out.check_flushed());
    }

    SECTION("no overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<dt88>{{0, 0}});
        elem_hist = {2, 0, 0};
        REQUIRE(
            out.check(element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

        SECTION("overflow during cycle 0, element 1") {
            REQUIRE_THROWS_AS(
                in.feed(bin_increment_batch_event<dt88>{{1, 1, 1, 1, 1, 1}}),
                end_processing);
            hist_arr = {0, 0, 0, 0, 0, 0};
            REQUIRE(out.check(
                concluding_histogram_array_event<dt88>{tmp_bucket(hist_arr)}));
            REQUIRE(out.check_flushed());
        }

        SECTION("no overflow during cycle 0, element 1") {
            in.feed(bin_increment_batch_event<dt88>{{1, 1}});
            elem_hist = {0, 2, 0};
            REQUIRE(out.check(
                element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
            hist_arr = {2, 0, 0, 0, 2, 0};
            REQUIRE(
                out.check(histogram_array_event<dt88>{tmp_bucket(hist_arr)}));

            SECTION("overflow during cycle 1, element 0") {
                REQUIRE_THROWS_AS(
                    in.feed(bin_increment_batch_event<dt88>{{0, 0, 0}}),
                    end_processing);
                hist_arr = {2, 0, 0, 0, 2, 0};
                REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                    tmp_bucket(hist_arr)}));
                REQUIRE(out.check_flushed());
            }

            SECTION("no overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<dt88>{{0}});
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

                SECTION("overflow during cycle 1, element 1") {
                    REQUIRE_THROWS_AS(
                        in.feed(bin_increment_batch_event<dt88>{{1, 1, 1}}),
                        end_processing);
                    hist_arr = {2, 0, 0, 0, 2, 0}; // Rolled back
                    REQUIRE(out.check(concluding_histogram_array_event<dt88>{
                        tmp_bucket(hist_arr)}));
                    REQUIRE(out.check_flushed());
                }
            }
        }
    }
}

TEST_CASE(
    "histogram_elementwise_accumulate with error-on-overflow, emit-concluding",
    "[histogram_elementwise_accumulate]") {
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, error_on_overflow, true,
                                         dt88>(
            2, 3, 4, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events>(
        ctx->access<capture_output_access>("out"));

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("overflow during cycle 0, element 0") {
        REQUIRE_THROWS_AS(
            in.feed(bin_increment_batch_event<dt88>{{0, 0, 0, 0, 0}}),
            histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("no overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<dt88>{{0, 0}});
        elem_hist = {2, 0, 0};
        REQUIRE(
            out.check(element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

        SECTION("overflow during cycle 0, element 1") {
            REQUIRE_THROWS_AS(
                in.feed(bin_increment_batch_event<dt88>{{1, 1, 1, 1, 1, 1}}),
                histogram_overflow_error);
            REQUIRE(out.check_not_flushed());
        }

        SECTION("no overflow during cycle 0, element 1") {
            in.feed(bin_increment_batch_event<dt88>{{1, 1}});
            elem_hist = {0, 2, 0};
            REQUIRE(out.check(
                element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
            hist_arr = {2, 0, 0, 0, 2, 0};
            REQUIRE(
                out.check(histogram_array_event<dt88>{tmp_bucket(hist_arr)}));

            SECTION("overflow during cycle 1, element 0") {
                REQUIRE_THROWS_AS(
                    in.feed(bin_increment_batch_event<dt88>{{0, 0, 0}}),
                    histogram_overflow_error);
                REQUIRE(out.check_not_flushed());
            }

            SECTION("no overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<dt88>{{0}});
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

                SECTION("overflow during cycle 1, element 1") {
                    REQUIRE_THROWS_AS(
                        in.feed(bin_increment_batch_event<dt88>{{1, 1, 1}}),
                        histogram_overflow_error);
                    REQUIRE(out.check_not_flushed());
                }
            }
        }
    }
}

TEST_CASE(
    "histogram_elementwise_accumulate with error-on-overflow, no emit-concluding",
    "[histogram_elementwise_accumulate]") {
    auto ctx = processor_context::create();
    auto in = feed_input<hea_input_events>(
        histogram_elementwise_accumulate<reset_event, error_on_overflow, false,
                                         dt88>(
            2, 3, 4, new_delete_bucket_source<u8>::create(),
            capture_output<hea_output_events_no_concluding>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<hea_output_events_no_concluding>(
        ctx->access<capture_output_access>("out"));

    std::vector<u8> elem_hist;
    std::vector<u8> hist_arr;

    SECTION("overflow during cycle 0, element 0") {
        REQUIRE_THROWS_AS(
            in.feed(bin_increment_batch_event<dt88>{{0, 0, 0, 0, 0}}),
            histogram_overflow_error);
        REQUIRE(out.check_not_flushed());
    }

    SECTION("no overflow during cycle 0, element 0") {
        in.feed(bin_increment_batch_event<dt88>{{0, 0}});
        elem_hist = {2, 0, 0};
        REQUIRE(
            out.check(element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

        SECTION("overflow during cycle 0, element 1") {
            REQUIRE_THROWS_AS(
                in.feed(bin_increment_batch_event<dt88>{{1, 1, 1, 1, 1, 1}}),
                histogram_overflow_error);
            REQUIRE(out.check_not_flushed());
        }

        SECTION("no overflow during cycle 0, element 1") {
            in.feed(bin_increment_batch_event<dt88>{{1, 1}});
            elem_hist = {0, 2, 0};
            REQUIRE(out.check(
                element_histogram_event<dt88>{tmp_bucket(elem_hist)}));
            hist_arr = {2, 0, 0, 0, 2, 0};
            REQUIRE(
                out.check(histogram_array_event<dt88>{tmp_bucket(hist_arr)}));

            SECTION("overflow during cycle 1, element 0") {
                REQUIRE_THROWS_AS(
                    in.feed(bin_increment_batch_event<dt88>{{0, 0, 0}}),
                    histogram_overflow_error);
                REQUIRE(out.check_not_flushed());
            }

            SECTION("no overflow during cycle 1, element 0") {
                in.feed(bin_increment_batch_event<dt88>{{0}});
                elem_hist = {3, 0, 0};
                REQUIRE(out.check(
                    element_histogram_event<dt88>{tmp_bucket(elem_hist)}));

                SECTION("overflow during cycle 1, element 1") {
                    REQUIRE_THROWS_AS(
                        in.feed(bin_increment_batch_event<dt88>{{1, 1, 1}}),
                        histogram_overflow_error);
                    REQUIRE(out.check_not_flushed());
                }
            }
        }
    }
}

} // namespace tcspc
