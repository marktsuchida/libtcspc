/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/binning.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/data_types.hpp"
#include "libtcspc/histogram_events.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/time_tagged_events.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace tcspc {

namespace {

using start_event = time_tagged_test_event<0>;
using stop_event = time_tagged_test_event<1>;
using misc_event = time_tagged_test_event<2>;

} // namespace

TEST_CASE("type constraints: map_to_datapoints") {
    struct data_types : default_data_types {
        using datapoint_type = u32;
    };
    using proc_type =
        decltype(map_to_datapoints<bulk_counts_event<>, data_types>(
            count_data_mapper<data_types>(),
            sink_events<datapoint_event<data_types>, int>()));
    STATIC_CHECK(is_processor_v<proc_type, bulk_counts_event<>>);
    STATIC_CHECK(handles_event_v<proc_type, int>);
    struct some_type {};
    STATIC_CHECK_FALSE(handles_event_v<proc_type, some_type>);
}

TEST_CASE("type constraints: map_to_bins") {
    struct data_types : default_data_types {
        using bin_index_type = u8;
    };
    using proc_type = decltype(map_to_bins<data_types>(
        power_of_2_bin_mapper<16, 8, false, data_types>(),
        sink_events<bin_increment_event<data_types>, int>()));
    STATIC_CHECK(is_processor_v<proc_type, datapoint_event<>>);
    STATIC_CHECK(handles_event_v<proc_type, int>);
    struct some_type {};
    STATIC_CHECK_FALSE(handles_event_v<proc_type, some_type>);
}

TEST_CASE("type constraints: batch_bin_increments") {
    struct data_types : default_data_types {
        using bin_index_type = u8;
    };
    using proc_type =
        decltype(batch_bin_increments<start_event, stop_event, data_types>(
            sink_events<bin_increment_batch_event<data_types>, int>()));
    STATIC_CHECK(is_processor_v<proc_type, start_event, stop_event,
                                bin_increment_event<>>);
    STATIC_CHECK(handles_event_v<proc_type, int>);
    struct some_type {};
    STATIC_CHECK_FALSE(handles_event_v<proc_type, some_type>);
}

TEST_CASE("introspect: binning") {
    struct data_types : default_data_types {
        using datapoint_type = u32;
    };
    check_introspect_simple_processor(
        map_to_datapoints<bulk_counts_event<data_types>, data_types>(
            count_data_mapper<data_types>(), null_sink()));
    check_introspect_simple_processor(
        map_to_bins(linear_bin_mapper(arg::offset{0}, arg::bin_width{1},
                                      arg::max_bin_index<u16>{10}),
                    null_sink()));
    check_introspect_simple_processor(
        batch_bin_increments<start_event, stop_event>(null_sink()));
}

TEST_CASE("Map to datapoints") {
    struct data_types : default_data_types {
        using datapoint_type = difftime_type;
    };
    using out_events = type_list<datapoint_event<data_types>, misc_event>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat,
        map_to_datapoints<time_correlated_detection_event<>, data_types>(
            difftime_data_mapper<data_types>(),
            capture_output<out_events>(
                ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    in.handle(misc_event{42});
    REQUIRE(out.check(emitted_as::same_as_fed, misc_event{42}));
    in.handle(time_correlated_detection_event<>{123, 0, 42});
    REQUIRE(out.check(datapoint_event<data_types>{42}));
    in.flush();
    REQUIRE(out.check_flushed());
}

TEST_CASE("Map to bins") {
    struct data_types : default_data_types {
        using datapoint_type = i32;
        using bin_index_type = u32;
    };

    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();

    SECTION("Out of range") {
        using out_events =
            type_list<bin_increment_event<data_types>, misc_event>;
        auto in = feed_input(
            valcat,
            map_to_bins<data_types>(
                []([[maybe_unused]] i32 d) { return std::optional<u32>(); },
                capture_output<out_events>(
                    ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.handle(misc_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, misc_event{42}));
        in.handle(datapoint_event<data_types>{123});
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Simple mapping") {
        struct add_42_bin_mapper {
            auto operator()(i32 d) const -> std::optional<u32> {
                return u32(d) + 42u;
            }
        };
        using out_events = type_list<bin_increment_event<data_types>>;
        auto in = feed_input(
            valcat, map_to_bins<data_types>(
                        add_42_bin_mapper(),
                        capture_output<out_events>(
                            ctx->tracker<capture_output_access>("out"))));
        in.require_output_checked(ctx, "out");
        auto out = capture_output_checker<out_events>(valcat, ctx, "out");

        in.handle(datapoint_event<data_types>{10});
        REQUIRE(out.check(bin_increment_event<data_types>{52}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("Power-of-2 bin mapping") {
    // NOLINTBEGIN(bugprone-unchecked-optional-access)

    struct data_types : default_data_types {
        using datapoint_type = u32;
        using bin_index_type = u16;
    };

    power_of_2_bin_mapper<0, 0, false, data_types> const m00;
    REQUIRE(m00.n_bins() == 1);
    REQUIRE(m00(0).value() == 0);
    REQUIRE_FALSE(m00(1));

    power_of_2_bin_mapper<0, 0, true, data_types> const m00f;
    REQUIRE(m00f.n_bins() == 1);
    REQUIRE(m00f(0).value() == 0);
    REQUIRE_FALSE(m00f(1));

    power_of_2_bin_mapper<1, 0, false, data_types> const m10;
    REQUIRE(m10.n_bins() == 1);
    REQUIRE(m10(0).value() == 0);
    REQUIRE(m10(1).value() == 0);
    REQUIRE_FALSE(m10(2));

    power_of_2_bin_mapper<1, 0, true, data_types> const m10f;
    REQUIRE(m10f.n_bins() == 1);
    REQUIRE(m10f(0).value() == 0);
    REQUIRE(m10f(1).value() == 0);
    REQUIRE_FALSE(m10f(2));

    power_of_2_bin_mapper<1, 1, false, data_types> const m11;
    REQUIRE(m11.n_bins() == 2);
    REQUIRE(m11(0).value() == 0);
    REQUIRE(m11(1).value() == 1);
    REQUIRE_FALSE(m11(2));

    power_of_2_bin_mapper<1, 1, true, data_types> const m11f;
    REQUIRE(m11f.n_bins() == 2);
    REQUIRE(m11f(0).value() == 1);
    REQUIRE(m11f(1).value() == 0);
    REQUIRE_FALSE(m11f(2));

    power_of_2_bin_mapper<2, 0, false, data_types> const m20;
    REQUIRE(m20.n_bins() == 1);
    REQUIRE(m20(0).value() == 0);
    REQUIRE(m20(1).value() == 0);
    REQUIRE(m20(2).value() == 0);
    REQUIRE(m20(3).value() == 0);
    REQUIRE_FALSE(m20(4));

    power_of_2_bin_mapper<2, 0, true, data_types> const m20f;
    REQUIRE(m20f.n_bins() == 1);
    REQUIRE(m20f(0).value() == 0);
    REQUIRE(m20f(1).value() == 0);
    REQUIRE(m20f(2).value() == 0);
    REQUIRE(m20f(3).value() == 0);
    REQUIRE_FALSE(m20f(4));

    power_of_2_bin_mapper<2, 1, false, data_types> const m21;
    REQUIRE(m21.n_bins() == 2);
    REQUIRE(m21(0).value() == 0);
    REQUIRE(m21(1).value() == 0);
    REQUIRE(m21(2).value() == 1);
    REQUIRE(m21(3).value() == 1);
    REQUIRE_FALSE(m21(4));

    power_of_2_bin_mapper<2, 1, true, data_types> const m21f;
    REQUIRE(m21f.n_bins() == 2);
    REQUIRE(m21f(0).value() == 1);
    REQUIRE(m21f(1).value() == 1);
    REQUIRE(m21f(2).value() == 0);
    REQUIRE(m21f(3).value() == 0);
    REQUIRE_FALSE(m21f(4));

    power_of_2_bin_mapper<2, 2, false, data_types> const m22;
    REQUIRE(m22.n_bins() == 4);
    REQUIRE(m22(0).value() == 0);
    REQUIRE(m22(1).value() == 1);
    REQUIRE(m22(2).value() == 2);
    REQUIRE(m22(3).value() == 3);
    REQUIRE_FALSE(m22(4));

    power_of_2_bin_mapper<2, 2, true, data_types> const m22f;
    REQUIRE(m22f.n_bins() == 4);
    REQUIRE(m22f(0).value() == 3);
    REQUIRE(m22f(1).value() == 2);
    REQUIRE(m22f(2).value() == 1);
    REQUIRE(m22f(3).value() == 0);
    REQUIRE_FALSE(m22f(4));

    power_of_2_bin_mapper<12, 8, false, data_types> const m12_8;
    REQUIRE(m12_8.n_bins() == 256);
    REQUIRE(m12_8(0).value() == 0);
    REQUIRE(m12_8(15).value() == 0);
    REQUIRE(m12_8(16).value() == 1);
    REQUIRE(m12_8(4095).value() == 255);
    REQUIRE_FALSE(m12_8(4096));

    power_of_2_bin_mapper<12, 8, true, data_types> const m12_8f;
    REQUIRE(m12_8f.n_bins() == 256);
    REQUIRE(m12_8f(0).value() == 255);
    REQUIRE(m12_8f(15).value() == 255);
    REQUIRE(m12_8f(16).value() == 254);
    REQUIRE(m12_8f(4095).value() == 0);
    REQUIRE_FALSE(m12_8f(4096));

    power_of_2_bin_mapper<16, 16, false, data_types> const m16_16;
    REQUIRE(m16_16.n_bins() == 65536);
    REQUIRE(m16_16(0).value() == 0);
    REQUIRE(m16_16(1).value() == 1);
    REQUIRE(m16_16(65535).value() == 65535);

    struct data_types_16_16 : data_types {
        using datapoint_type = u16;
    };
    power_of_2_bin_mapper<16, 16, false, data_types_16_16> const m16_16_16;
    REQUIRE(m16_16_16.n_bins() == 65536);
    REQUIRE(m16_16_16(0).value() == 0);
    REQUIRE(m16_16_16(1).value() == 1);
    REQUIRE(m16_16_16(65535).value() == 65535);

    power_of_2_bin_mapper<32, 16, false, data_types> const m32_16;
    REQUIRE(m32_16.n_bins() == 65536);
    REQUIRE(m32_16(0).value() == 0);
    REQUIRE(m32_16(65535).value() == 0);
    REQUIRE(m32_16(65536).value() == 1);
    auto m = std::numeric_limits<u32>::max();
    REQUIRE(m32_16(m - 65536).value() == 65534);
    REQUIRE(m32_16(m - 65535).value() == 65535);
    REQUIRE(m32_16(m).value() == 65535);

    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("power_of_2_bin_mapper with signed datapoint_type") {
    // NOLINTBEGIN(bugprone-unchecked-optional-access)

    struct data_types {
        using datapoint_type = i32;
        using bin_index_type = u8;
    };
    power_of_2_bin_mapper<16, 8, false, data_types> const m;
    CHECK(m(0).value() == 0);
    CHECK(m(65535).value() == 255);
    CHECK_FALSE(m(65536).has_value());
    CHECK_FALSE(m(-1).has_value());
    CHECK_FALSE(m(std::numeric_limits<i32>::max()).has_value());
    CHECK_FALSE(m(std::numeric_limits<i32>::min()).has_value());

    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("Linear bin mapping") {
    // NOLINTBEGIN(bugprone-unchecked-optional-access)

    struct data_types : default_data_types {
        using datapoint_type = i32;
        using bin_index_type = u16;
    };

    bool const clamp = GENERATE(false, true);
    auto check_clamped = [=](std::optional<u16> o, u16 clamped) {
        if (clamp)
            return o.value() == clamped;
        return !o;
    };

    linear_bin_mapper<data_types> const m010{
        arg::offset{0}, arg::bin_width{1}, arg::max_bin_index<u16>{0}, clamp};
    REQUIRE(m010.n_bins() == 1);
    REQUIRE(check_clamped(m010(-1), 0));
    REQUIRE(m010(0).value() == 0);
    REQUIRE(check_clamped(m010(1), 0));

    linear_bin_mapper<data_types> const m110{
        arg::offset{1}, arg::bin_width{1}, arg::max_bin_index<u16>{0}, clamp};
    REQUIRE(m110.n_bins() == 1);
    REQUIRE(check_clamped(m110(0), 0));
    REQUIRE(m110(1).value() == 0);
    REQUIRE(check_clamped(m110(2), 0));

    linear_bin_mapper<data_types> const nn10{
        arg::offset{-1}, arg::bin_width{1}, arg::max_bin_index<u16>{0}, clamp};
    REQUIRE(nn10.n_bins() == 1);
    REQUIRE(check_clamped(nn10(-2), 0));
    REQUIRE(nn10(-1).value() == 0);
    REQUIRE(check_clamped(nn10(0), 0));

    linear_bin_mapper<data_types> const m020{
        arg::offset{0}, arg::bin_width{2}, arg::max_bin_index<u16>{0}, clamp};
    REQUIRE(m020.n_bins() == 1);
    REQUIRE(check_clamped(m020(-1), 0));
    REQUIRE(m020(0).value() == 0);
    REQUIRE(m020(1).value() == 0);
    REQUIRE(check_clamped(m020(2), 0));

    linear_bin_mapper<data_types> const m120{
        arg::offset{1}, arg::bin_width{2}, arg::max_bin_index<u16>{0}, clamp};
    REQUIRE(m120.n_bins() == 1);
    REQUIRE(check_clamped(m120(0), 0));
    REQUIRE(m120(1).value() == 0);
    REQUIRE(m120(2).value() == 0);
    REQUIRE(check_clamped(m120(3), 0));

    linear_bin_mapper<data_types> const mn20{
        arg::offset{-1}, arg::bin_width{2}, arg::max_bin_index<u16>{0}, clamp};
    REQUIRE(mn20.n_bins() == 1);
    REQUIRE(check_clamped(mn20(-2), 0));
    REQUIRE(mn20(-1).value() == 0);
    REQUIRE(mn20(0).value() == 0);
    REQUIRE(check_clamped(mn20(1), 0));

    linear_bin_mapper<data_types> const m0n0{
        arg::offset{0}, arg::bin_width{-1}, arg::max_bin_index<u16>{0}, clamp};
    REQUIRE(m0n0.n_bins() == 1);
    REQUIRE(check_clamped(m0n0(1), 0));
    REQUIRE(m0n0(0).value() == 0);
    REQUIRE(check_clamped(m0n0(-1), 0));

    linear_bin_mapper<data_types> const m1n0{
        arg::offset{1}, arg::bin_width{-1}, arg::max_bin_index<u16>{0}, clamp};
    REQUIRE(m1n0.n_bins() == 1);
    REQUIRE(check_clamped(m1n0(2), 0));
    REQUIRE(m1n0(1).value() == 0);
    REQUIRE(check_clamped(m1n0(0), 0));

    linear_bin_mapper<data_types> const mnn0{
        arg::offset{-1}, arg::bin_width{-1}, arg::max_bin_index<u16>{0},
        clamp};
    REQUIRE(mnn0.n_bins() == 1);
    REQUIRE(check_clamped(mnn0(0), 0));
    REQUIRE(mnn0(-1).value() == 0);
    REQUIRE(check_clamped(mnn0(-2), 0));

    linear_bin_mapper<data_types> const m011{
        arg::offset{0}, arg::bin_width{1}, arg::max_bin_index<u16>{1}, clamp};
    REQUIRE(m011.n_bins() == 2);
    REQUIRE(check_clamped(m011(-1), 0));
    REQUIRE(m011(0).value() == 0);
    REQUIRE(m011(1).value() == 1);
    REQUIRE(check_clamped(m011(2), 1));

    linear_bin_mapper<data_types> const m111{
        arg::offset{1}, arg::bin_width{1}, arg::max_bin_index<u16>{1}, clamp};
    REQUIRE(m111.n_bins() == 2);
    REQUIRE(check_clamped(m111(0), 0));
    REQUIRE(m111(1).value() == 0);
    REQUIRE(m111(2).value() == 1);
    REQUIRE(check_clamped(m111(3), 1));

    linear_bin_mapper<data_types> const mn11{
        arg::offset{-1}, arg::bin_width{1}, arg::max_bin_index<u16>{1}, clamp};
    REQUIRE(mn11.n_bins() == 2);
    REQUIRE(check_clamped(mn11(-2), 0));
    REQUIRE(mn11(-1).value() == 0);
    REQUIRE(mn11(0).value() == 1);
    REQUIRE(check_clamped(mn11(1), 1));

    linear_bin_mapper<data_types> const m0n1{
        arg::offset{0}, arg::bin_width{-1}, arg::max_bin_index<u16>{1}, clamp};
    REQUIRE(m0n1.n_bins() == 2);
    REQUIRE(check_clamped(m0n1(1), 0));
    REQUIRE(m0n1(0).value() == 0);
    REQUIRE(m0n1(-1).value() == 1);
    REQUIRE(check_clamped(m0n1(-2), 1));

    linear_bin_mapper<data_types> const m1n1{
        arg::offset{1}, arg::bin_width{-1}, arg::max_bin_index<u16>{1}, clamp};
    REQUIRE(m1n1.n_bins() == 2);
    REQUIRE(check_clamped(m1n1(2), 0));
    REQUIRE(m1n1(1).value() == 0);
    REQUIRE(m1n1(0).value() == 1);
    REQUIRE(check_clamped(m1n1(-1), 1));

    linear_bin_mapper<data_types> const mnn1{
        arg::offset{-1}, arg::bin_width{-1}, arg::max_bin_index<u16>{1},
        clamp};
    REQUIRE(mnn1.n_bins() == 2);
    REQUIRE(check_clamped(mnn1(0), 0));
    REQUIRE(mnn1(-1).value() == 0);
    REQUIRE(mnn1(-2).value() == 1);
    REQUIRE(check_clamped(mnn1(-3), 1));

    linear_bin_mapper<data_types> const maxint{
        arg::offset{0}, arg::bin_width{32768}, arg::max_bin_index<u16>{65535},
        clamp};
    REQUIRE(maxint.n_bins() == 65536);
    REQUIRE(maxint(0).value() == 0);
    REQUIRE(maxint(32767).value() == 0);
    REQUIRE(maxint(32768).value() == 1);
    REQUIRE(maxint(std::numeric_limits<i32>::max()) == 65535);

    struct data_types_u32 : data_types {
        using datapoint_type = u32;
    };
    linear_bin_mapper<data_types_u32> const maxuint{
        arg::offset{0u}, arg::bin_width{65536u},
        arg::max_bin_index<u16>{65535}, clamp};
    REQUIRE(maxuint.n_bins() == 65536);
    REQUIRE(maxuint(0).value() == 0);
    REQUIRE(maxuint(65535).value() == 0);
    REQUIRE(maxuint(65536).value() == 1);
    REQUIRE(maxuint(std::numeric_limits<u32>::max()) == 65535);

    // Typical flipped 12-bit -> 8-bit
    linear_bin_mapper<data_types> const flipped{
        arg::offset{4095}, arg::bin_width{-16}, arg::max_bin_index<u16>{255},
        clamp};
    REQUIRE(flipped.n_bins() == 256);
    REQUIRE(flipped(0).value() == 255);
    REQUIRE(flipped(15).value() == 255);
    REQUIRE(flipped(16).value() == 254);
    REQUIRE(flipped(4095 - 16).value() == 1);
    REQUIRE(flipped(4095 - 15).value() == 0);
    REQUIRE(flipped(4095).value() == 0);
    REQUIRE(check_clamped(flipped(4096), 0));
    REQUIRE(check_clamped(flipped(65535), 0));

    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("unique_bin_mapper") {
    using datapoint_type = default_data_types::datapoint_type;
    using bin_index_type = default_data_types::bin_index_type;
    auto ctx = context::create();
    unique_bin_mapper<> m1(
        ctx->tracker<unique_bin_mapper_access<datapoint_type>>("m1"),
        arg::max_bin_index<bin_index_type>{3});
    CHECK(m1.n_bins() == 4);
    auto access = ctx->access<unique_bin_mapper_access<datapoint_type>>("m1");
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    CHECK(access.values().empty());
    CHECK(m1(42).value() == 0);
    CHECK(access.values() == std::vector<datapoint_type>{42});
    CHECK(m1(43).value() == 1);
    CHECK(access.values() == std::vector<datapoint_type>{42, 43});
    CHECK(m1(42).value() == 0);
    CHECK(access.values() == std::vector<datapoint_type>{42, 43});
    CHECK(m1(40).value() == 2);
    CHECK(access.values() == std::vector<datapoint_type>{42, 43, 40});
    CHECK(m1(45).value() == 3);
    CHECK(access.values() == std::vector<datapoint_type>{42, 43, 40, 45});
    CHECK_FALSE(m1(44).has_value());
    // NOLINTEND(bugprone-unchecked-optional-access)
    CHECK(access.values() == std::vector<datapoint_type>{42, 43, 40, 45, 44});
}

TEST_CASE("Batch bin increments") {
    struct data_types : default_data_types {
        using bin_index_type = u32;
    };
    using out_events =
        type_list<bin_increment_batch_event<data_types>, misc_event>;
    auto const valcat = GENERATE(feed_as::const_lvalue, feed_as::rvalue);
    auto ctx = context::create();
    auto in = feed_input(
        valcat, batch_bin_increments<start_event, stop_event, data_types>(
                    capture_output<out_events>(
                        ctx->tracker<capture_output_access>("out"))));
    in.require_output_checked(ctx, "out");
    auto out = capture_output_checker<out_events>(valcat, ctx, "out");

    SECTION("Pass through unrelated") {
        in.handle(misc_event{42});
        REQUIRE(out.check(emitted_as::same_as_fed, misc_event{42}));
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Stop before first start ignored") {
        in.handle(stop_event{42});
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Start with no stop ignored") {
        in.handle(start_event{42});
        in.handle(bin_increment_event<data_types>{123});
        in.flush();
        REQUIRE(out.check_flushed());
    }

    SECTION("Events passed only between start and stop") {
        in.handle(start_event{42});
        in.handle(bin_increment_event<data_types>{123});
        in.handle(stop_event{44});
        REQUIRE(out.check(bin_increment_batch_event<data_types>{{123}}));
        in.handle(start_event{45});
        in.handle(bin_increment_event<data_types>{124});
        in.handle(bin_increment_event<data_types>{125});
        in.handle(stop_event{48});
        REQUIRE(out.check(bin_increment_batch_event<data_types>{{124, 125}}));
        in.flush();
        REQUIRE(out.check_flushed());
    }
}

} // namespace tcspc
