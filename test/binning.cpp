/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/binning.hpp"

#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"

#include <catch2/catch_all.hpp>

#include <limits>
#include <utility>

using namespace tcspc;

using start_event = timestamped_test_event<0>;
using stop_event = timestamped_test_event<1>;
using misc_event = timestamped_test_event<2>;

TEST_CASE("Map to datapoints", "[map_to_datapoints][difftime_data_mapper]") {
    auto out = capture_output<event_set<datapoint_event<u16>, misc_event>>();
    auto in =
        feed_input<event_set<time_correlated_detection_event, misc_event>>(
            map_to_datapoints(difftime_data_mapper(), ref_processor(out)));
    in.require_output_checked(out);

    in.feed(misc_event{42});
    REQUIRE(out.check(misc_event{42}));
    in.feed(time_correlated_detection_event{{{123}, 0}, 42});
    REQUIRE(out.check(datapoint_event<u16>{123, 42}));
    in.feed_end();
    REQUIRE(out.check_end());
}

TEST_CASE("Map to bins", "[map_to_bin]") {
    SECTION("Out of range") {
        struct null_bin_mapper {
            using data_type = i32;
            using bin_index_type = u32;
            auto operator()([[maybe_unused]] int d) const noexcept
                -> std::optional<unsigned> {
                return std::nullopt;
            }
        };

        auto out =
            capture_output<event_set<bin_increment_event<u32>, misc_event>>();
        auto in = feed_input<event_set<datapoint_event<i32>, misc_event>>(
            map_to_bins(null_bin_mapper(), ref_processor(out)));
        in.require_output_checked(out);

        in.feed(misc_event{42});
        REQUIRE(out.check(misc_event{42}));
        in.feed(datapoint_event<i32>{43, 123});
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Simple mapping") {
        struct add_42_bin_mapper {
            using data_type = i32;
            using bin_index_type = u32;
            auto operator()(int d) const noexcept -> std::optional<unsigned> {
                return unsigned(d) + 42u;
            }
        };

        auto out = capture_output<event_set<bin_increment_event<u32>>>();
        auto in = feed_input<event_set<datapoint_event<i32>>>(
            map_to_bins(add_42_bin_mapper(), ref_processor(out)));
        in.require_output_checked(out);

        in.feed(datapoint_event<i32>{0, 10});
        REQUIRE(out.check(bin_increment_event<u32>{0, 52}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}

TEST_CASE("Power-of-2 bin mapping", "[power_of_2_bin_mapper]") {
    power_of_2_bin_mapper<u32, u16, 0, 0, false> const m00;
    REQUIRE(m00.n_bins() == 1);
    REQUIRE(m00(0).value() == 0);
    REQUIRE_FALSE(m00(1));

    power_of_2_bin_mapper<u32, u16, 0, 0, true> const m00f;
    REQUIRE(m00f.n_bins() == 1);
    REQUIRE(m00f(0).value() == 0);
    REQUIRE_FALSE(m00f(1));

    power_of_2_bin_mapper<u32, u16, 1, 0, false> const m10;
    REQUIRE(m10.n_bins() == 1);
    REQUIRE(m10(0).value() == 0);
    REQUIRE(m10(1).value() == 0);
    REQUIRE_FALSE(m10(2));

    power_of_2_bin_mapper<u32, u16, 1, 0, true> const m10f;
    REQUIRE(m10f.n_bins() == 1);
    REQUIRE(m10f(0).value() == 0);
    REQUIRE(m10f(1).value() == 0);
    REQUIRE_FALSE(m10f(2));

    power_of_2_bin_mapper<u32, u16, 1, 1, false> const m11;
    REQUIRE(m11.n_bins() == 2);
    REQUIRE(m11(0).value() == 0);
    REQUIRE(m11(1).value() == 1);
    REQUIRE_FALSE(m11(2));

    power_of_2_bin_mapper<u32, u16, 1, 1, true> const m11f;
    REQUIRE(m11f.n_bins() == 2);
    REQUIRE(m11f(0).value() == 1);
    REQUIRE(m11f(1).value() == 0);
    REQUIRE_FALSE(m11f(2));

    power_of_2_bin_mapper<u32, u16, 2, 0, false> const m20;
    REQUIRE(m20.n_bins() == 1);
    REQUIRE(m20(0).value() == 0);
    REQUIRE(m20(1).value() == 0);
    REQUIRE(m20(2).value() == 0);
    REQUIRE(m20(3).value() == 0);
    REQUIRE_FALSE(m20(4));

    power_of_2_bin_mapper<u32, u16, 2, 0, true> const m20f;
    REQUIRE(m20f.n_bins() == 1);
    REQUIRE(m20f(0).value() == 0);
    REQUIRE(m20f(1).value() == 0);
    REQUIRE(m20f(2).value() == 0);
    REQUIRE(m20f(3).value() == 0);
    REQUIRE_FALSE(m20f(4));

    power_of_2_bin_mapper<u32, u16, 2, 1, false> const m21;
    REQUIRE(m21.n_bins() == 2);
    REQUIRE(m21(0).value() == 0);
    REQUIRE(m21(1).value() == 0);
    REQUIRE(m21(2).value() == 1);
    REQUIRE(m21(3).value() == 1);
    REQUIRE_FALSE(m21(4));

    power_of_2_bin_mapper<u32, u16, 2, 1, true> const m21f;
    REQUIRE(m21f.n_bins() == 2);
    REQUIRE(m21f(0).value() == 1);
    REQUIRE(m21f(1).value() == 1);
    REQUIRE(m21f(2).value() == 0);
    REQUIRE(m21f(3).value() == 0);
    REQUIRE_FALSE(m21f(4));

    power_of_2_bin_mapper<u32, u16, 2, 2, false> const m22;
    REQUIRE(m22.n_bins() == 4);
    REQUIRE(m22(0).value() == 0);
    REQUIRE(m22(1).value() == 1);
    REQUIRE(m22(2).value() == 2);
    REQUIRE(m22(3).value() == 3);
    REQUIRE_FALSE(m22(4));

    power_of_2_bin_mapper<u32, u16, 2, 2, true> const m22f;
    REQUIRE(m22f.n_bins() == 4);
    REQUIRE(m22f(0).value() == 3);
    REQUIRE(m22f(1).value() == 2);
    REQUIRE(m22f(2).value() == 1);
    REQUIRE(m22f(3).value() == 0);
    REQUIRE_FALSE(m22f(4));

    power_of_2_bin_mapper<u32, u16, 12, 8, false> const m12_8;
    REQUIRE(m12_8.n_bins() == 256);
    REQUIRE(m12_8(0).value() == 0);
    REQUIRE(m12_8(15).value() == 0);
    REQUIRE(m12_8(16).value() == 1);
    REQUIRE(m12_8(4095).value() == 255);
    REQUIRE_FALSE(m12_8(4096));

    power_of_2_bin_mapper<u32, u16, 12, 8, true> const m12_8f;
    REQUIRE(m12_8f.n_bins() == 256);
    REQUIRE(m12_8f(0).value() == 255);
    REQUIRE(m12_8f(15).value() == 255);
    REQUIRE(m12_8f(16).value() == 254);
    REQUIRE(m12_8f(4095).value() == 0);
    REQUIRE_FALSE(m12_8f(4096));

    power_of_2_bin_mapper<u32, u16, 16, 16, false> const m16_16;
    REQUIRE(m16_16.n_bins() == 65536);
    REQUIRE(m16_16(0).value() == 0);
    REQUIRE(m16_16(1).value() == 1);
    REQUIRE(m16_16(65535).value() == 65535);

    power_of_2_bin_mapper<u16, u16, 16, 16, false> const m16_16_16;
    REQUIRE(m16_16_16.n_bins() == 65536);
    REQUIRE(m16_16_16(0).value() == 0);
    REQUIRE(m16_16_16(1).value() == 1);
    REQUIRE(m16_16_16(65535).value() == 65535);

    power_of_2_bin_mapper<u32, u16, 32, 16, false> const m32_16;
    REQUIRE(m32_16.n_bins() == 65536);
    REQUIRE(m32_16(0).value() == 0);
    REQUIRE(m32_16(65535).value() == 0);
    REQUIRE(m32_16(65536).value() == 1);
    auto m = std::numeric_limits<u32>::max();
    REQUIRE(m32_16(m - 65536).value() == 65534);
    REQUIRE(m32_16(m - 65535).value() == 65535);
    REQUIRE(m32_16(m).value() == 65535);
}

TEST_CASE("Linear bin mapping", "[linear_bin_mapper]") {
    bool const clamp = GENERATE(false, true);
    auto check_clamped = [=](std::optional<u16> o, u16 clamped) {
        if (clamp)
            return o.value() == clamped;
        return !o;
    };

    linear_bin_mapper<i32, u16> const m010(0, 1, 0, clamp);
    REQUIRE(m010.n_bins() == 1);
    REQUIRE(check_clamped(m010(-1), 0));
    REQUIRE(m010(0).value() == 0);
    REQUIRE(check_clamped(m010(1), 0));

    linear_bin_mapper<i32, u16> const m110(1, 1, 0, clamp);
    REQUIRE(m110.n_bins() == 1);
    REQUIRE(check_clamped(m110(0), 0));
    REQUIRE(m110(1).value() == 0);
    REQUIRE(check_clamped(m110(2), 0));

    linear_bin_mapper<i32, u16> const nn10(-1, 1, 0, clamp);
    REQUIRE(nn10.n_bins() == 1);
    REQUIRE(check_clamped(nn10(-2), 0));
    REQUIRE(nn10(-1).value() == 0);
    REQUIRE(check_clamped(nn10(0), 0));

    linear_bin_mapper<i32, u16> const m020(0, 2, 0, clamp);
    REQUIRE(m020.n_bins() == 1);
    REQUIRE(check_clamped(m020(-1), 0));
    REQUIRE(m020(0).value() == 0);
    REQUIRE(m020(1).value() == 0);
    REQUIRE(check_clamped(m020(2), 0));

    linear_bin_mapper<i32, u16> const m120(1, 2, 0, clamp);
    REQUIRE(m120.n_bins() == 1);
    REQUIRE(check_clamped(m120(0), 0));
    REQUIRE(m120(1).value() == 0);
    REQUIRE(m120(2).value() == 0);
    REQUIRE(check_clamped(m120(3), 0));

    linear_bin_mapper<i32, u16> const mn20(-1, 2, 0, clamp);
    REQUIRE(mn20.n_bins() == 1);
    REQUIRE(check_clamped(mn20(-2), 0));
    REQUIRE(mn20(-1).value() == 0);
    REQUIRE(mn20(0).value() == 0);
    REQUIRE(check_clamped(mn20(1), 0));

    linear_bin_mapper<i32, u16> const m0n0(0, -1, 0, clamp);
    REQUIRE(m0n0.n_bins() == 1);
    REQUIRE(check_clamped(m0n0(1), 0));
    REQUIRE(m0n0(0).value() == 0);
    REQUIRE(check_clamped(m0n0(-1), 0));

    linear_bin_mapper<i32, u16> const m1n0(1, -1, 0, clamp);
    REQUIRE(m1n0.n_bins() == 1);
    REQUIRE(check_clamped(m1n0(2), 0));
    REQUIRE(m1n0(1).value() == 0);
    REQUIRE(check_clamped(m1n0(0), 0));

    linear_bin_mapper<i32, u16> const mnn0(-1, -1, 0, clamp);
    REQUIRE(mnn0.n_bins() == 1);
    REQUIRE(check_clamped(mnn0(0), 0));
    REQUIRE(mnn0(-1).value() == 0);
    REQUIRE(check_clamped(mnn0(-2), 0));

    linear_bin_mapper<i32, u16> const m011(0, 1, 1, clamp);
    REQUIRE(m011.n_bins() == 2);
    REQUIRE(check_clamped(m011(-1), 0));
    REQUIRE(m011(0).value() == 0);
    REQUIRE(m011(1).value() == 1);
    REQUIRE(check_clamped(m011(2), 1));

    linear_bin_mapper<i32, u16> const m111(1, 1, 1, clamp);
    REQUIRE(m111.n_bins() == 2);
    REQUIRE(check_clamped(m111(0), 0));
    REQUIRE(m111(1).value() == 0);
    REQUIRE(m111(2).value() == 1);
    REQUIRE(check_clamped(m111(3), 1));

    linear_bin_mapper<i32, u16> const mn11(-1, 1, 1, clamp);
    REQUIRE(mn11.n_bins() == 2);
    REQUIRE(check_clamped(mn11(-2), 0));
    REQUIRE(mn11(-1).value() == 0);
    REQUIRE(mn11(0).value() == 1);
    REQUIRE(check_clamped(mn11(1), 1));

    linear_bin_mapper<i32, u16> const m0n1(0, -1, 1, clamp);
    REQUIRE(m0n1.n_bins() == 2);
    REQUIRE(check_clamped(m0n1(1), 0));
    REQUIRE(m0n1(0).value() == 0);
    REQUIRE(m0n1(-1).value() == 1);
    REQUIRE(check_clamped(m0n1(-2), 1));

    linear_bin_mapper<i32, u16> const m1n1(1, -1, 1, clamp);
    REQUIRE(m1n1.n_bins() == 2);
    REQUIRE(check_clamped(m1n1(2), 0));
    REQUIRE(m1n1(1).value() == 0);
    REQUIRE(m1n1(0).value() == 1);
    REQUIRE(check_clamped(m1n1(-1), 1));

    linear_bin_mapper<i32, u16> const mnn1(-1, -1, 1, clamp);
    REQUIRE(mnn1.n_bins() == 2);
    REQUIRE(check_clamped(mnn1(0), 0));
    REQUIRE(mnn1(-1).value() == 0);
    REQUIRE(mnn1(-2).value() == 1);
    REQUIRE(check_clamped(mnn1(-3), 1));

    linear_bin_mapper<u32, u16> const maxint(0, 32768, 65535, clamp);
    REQUIRE(maxint.n_bins() == 65536);
    REQUIRE(maxint(0).value() == 0);
    REQUIRE(maxint(32767).value() == 0);
    REQUIRE(maxint(32768).value() == 1);
    REQUIRE(maxint(std::numeric_limits<i32>::max()) == 65535);

    linear_bin_mapper<u32, u16> const maxuint(0, 65536, 65535, clamp);
    REQUIRE(maxuint.n_bins() == 65536);
    REQUIRE(maxuint(0).value() == 0);
    REQUIRE(maxuint(65535).value() == 0);
    REQUIRE(maxuint(65536).value() == 1);
    REQUIRE(maxuint(std::numeric_limits<u32>::max()) == 65535);

    // Typical flipped 12-bit -> 8-bit
    linear_bin_mapper<i32, u16> const flipped(4095, -16, 255, clamp);
    REQUIRE(flipped.n_bins() == 256);
    REQUIRE(flipped(0).value() == 255);
    REQUIRE(flipped(15).value() == 255);
    REQUIRE(flipped(16).value() == 254);
    REQUIRE(flipped(4095 - 16).value() == 1);
    REQUIRE(flipped(4095 - 15).value() == 0);
    REQUIRE(flipped(4095).value() == 0);
    REQUIRE(check_clamped(flipped(4096), 0));
    REQUIRE(check_clamped(flipped(65535), 0));
}

TEST_CASE("Batch bin increments", "[batch_bin_increments]") {
    auto out = capture_output<
        event_set<bin_increment_batch_event<u32>, misc_event>>();
    auto in = feed_input<event_set<bin_increment_event<u32>, start_event,
                                   stop_event, misc_event>>(
        batch_bin_increments<u32, start_event, stop_event>(
            ref_processor(out)));
    in.require_output_checked(out);

    SECTION("Pass through unrelated") {
        in.feed(misc_event{42});
        REQUIRE(out.check(misc_event{42}));
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Stop before first start ignored") {
        in.feed(stop_event{42});
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Start with no stop ignored") {
        in.feed(start_event{42});
        in.feed(bin_increment_event<u32>{43, 123});
        in.feed_end();
        REQUIRE(out.check_end());
    }

    SECTION("Events passed only between start and stop") {
        in.feed(start_event{42});
        in.feed(bin_increment_event<u32>{43, 123});
        in.feed(stop_event{44});
        REQUIRE(out.check(bin_increment_batch_event<u32>{{42, 44}, {123}}));
        in.feed(start_event{45});
        in.feed(bin_increment_event<u32>{46, 124});
        in.feed(bin_increment_event<u32>{47, 125});
        in.feed(stop_event{48});
        REQUIRE(
            out.check(bin_increment_batch_event<u32>{{45, 48}, {124, 125}}));
        in.feed_end();
        REQUIRE(out.check_end());
    }
}
