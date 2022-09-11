/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/Binning.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/TimeTaggedEvents.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <catch2/catch.hpp>

#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

using namespace flimevt;
using namespace flimevt::test;

using DataMapInput = event_set<time_correlated_count_event, Event<0>>;
using DataMapOutput = event_set<datapoint_event<std::uint16_t>, Event<0>>;
using DataMapOutVec = std::vector<event_variant<DataMapOutput>>;

auto MakeMapDifftimeToDatapointsFixture() {
    return MakeProcessorTestFixture<DataMapInput, DataMapOutput>(
        [](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return map_to_datapoints<difftime_data_mapper, D>(
                difftime_data_mapper(), std::move(downstream));
        });
}

TEST_CASE("Map to datapoints", "[map_to_datapoints]") {
    auto f = MakeMapDifftimeToDatapointsFixture();

    f.FeedEvents({
        Event<0>{42},
    });
    REQUIRE(f.Output() == DataMapOutVec{
                              Event<0>{42},
                          });
    f.FeedEvents({
        time_correlated_count_event{{123}, 42, 0},
    });
    REQUIRE(f.Output() == DataMapOutVec{
                              datapoint_event<std::uint16_t>{123, 42},
                          });
    f.FeedEnd({});
    REQUIRE(f.Output() == DataMapOutVec{});
    REQUIRE(f.DidEnd());
}

using BinInput = event_set<datapoint_event<int>, Event<0>>;
using BinOutput = event_set<bin_increment_event<unsigned>, Event<0>>;
using BinOutVec = std::vector<event_variant<BinOutput>>;

template <typename F> auto MakeMapToBinsFixture(F mapFunc) {
    struct BinMapper {
        using data_type = int;
        using bin_index_type = unsigned;
        F f;
        std::optional<unsigned> operator()(int d) const noexcept {
            return f(d);
        }
    };
    return MakeProcessorTestFixture<BinInput, BinOutput>(
        [=](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return map_to_bins<BinMapper, D>(BinMapper{mapFunc},
                                             std::move(downstream));
        });
}

TEST_CASE("Map to bins", "[MapToBin]") {
    auto f = MakeMapToBinsFixture(
        []([[maybe_unused]] int d) -> std::optional<unsigned> {
            return std::nullopt;
        });
    f.FeedEvents({
        Event<0>{42},
    });
    REQUIRE(f.Output() == BinOutVec{
                              Event<0>{42},
                          });
    f.FeedEvents({
        datapoint_event<int>{123, 0},
    });
    REQUIRE(f.Output() == BinOutVec{});
    f.FeedEnd({});
    REQUIRE(f.Output() == BinOutVec{});
    REQUIRE(f.DidEnd());

    auto g = MakeMapToBinsFixture(
        [](int d) -> std::optional<unsigned> { return d + 123; });
    g.FeedEvents({
        datapoint_event<int>{123, 0},
    });
    REQUIRE(g.Output() == BinOutVec{
                              bin_increment_event<unsigned>{123, 123},
                          });
    g.FeedEnd({});
    REQUIRE(g.Output() == BinOutVec{});
    REQUIRE(g.DidEnd());
}

using i32 = std::int32_t;
using u32 = std::uint32_t;
using u16 = std::uint16_t;

TEST_CASE("Power-of-2 bin mapping", "[power_of_2_bin_mapper]") {
    power_of_2_bin_mapper<u32, u16, 0, 0, false> m00;
    REQUIRE(m00.get_n_bins() == 1);
    REQUIRE(m00(0).value() == 0);
    REQUIRE(!m00(1));

    power_of_2_bin_mapper<u32, u16, 0, 0, true> m00f;
    REQUIRE(m00f.get_n_bins() == 1);
    REQUIRE(m00f(0).value() == 0);
    REQUIRE(!m00f(1));

    power_of_2_bin_mapper<u32, u16, 1, 0, false> m10;
    REQUIRE(m10.get_n_bins() == 1);
    REQUIRE(m10(0).value() == 0);
    REQUIRE(m10(1).value() == 0);
    REQUIRE(!m10(2));

    power_of_2_bin_mapper<u32, u16, 1, 0, true> m10f;
    REQUIRE(m10f.get_n_bins() == 1);
    REQUIRE(m10f(0).value() == 0);
    REQUIRE(m10f(1).value() == 0);
    REQUIRE(!m10f(2));

    power_of_2_bin_mapper<u32, u16, 1, 1, false> m11;
    REQUIRE(m11.get_n_bins() == 2);
    REQUIRE(m11(0).value() == 0);
    REQUIRE(m11(1).value() == 1);
    REQUIRE(!m11(2));

    power_of_2_bin_mapper<u32, u16, 1, 1, true> m11f;
    REQUIRE(m11f.get_n_bins() == 2);
    REQUIRE(m11f(0).value() == 1);
    REQUIRE(m11f(1).value() == 0);
    REQUIRE(!m11f(2));

    power_of_2_bin_mapper<u32, u16, 2, 0, false> m20;
    REQUIRE(m20.get_n_bins() == 1);
    REQUIRE(m20(0).value() == 0);
    REQUIRE(m20(1).value() == 0);
    REQUIRE(m20(2).value() == 0);
    REQUIRE(m20(3).value() == 0);
    REQUIRE(!m20(4));

    power_of_2_bin_mapper<u32, u16, 2, 0, true> m20f;
    REQUIRE(m20f.get_n_bins() == 1);
    REQUIRE(m20f(0).value() == 0);
    REQUIRE(m20f(1).value() == 0);
    REQUIRE(m20f(2).value() == 0);
    REQUIRE(m20f(3).value() == 0);
    REQUIRE(!m20f(4));

    power_of_2_bin_mapper<u32, u16, 2, 1, false> m21;
    REQUIRE(m21.get_n_bins() == 2);
    REQUIRE(m21(0).value() == 0);
    REQUIRE(m21(1).value() == 0);
    REQUIRE(m21(2).value() == 1);
    REQUIRE(m21(3).value() == 1);
    REQUIRE(!m21(4));

    power_of_2_bin_mapper<u32, u16, 2, 1, true> m21f;
    REQUIRE(m21f.get_n_bins() == 2);
    REQUIRE(m21f(0).value() == 1);
    REQUIRE(m21f(1).value() == 1);
    REQUIRE(m21f(2).value() == 0);
    REQUIRE(m21f(3).value() == 0);
    REQUIRE(!m21f(4));

    power_of_2_bin_mapper<u32, u16, 2, 2, false> m22;
    REQUIRE(m22.get_n_bins() == 4);
    REQUIRE(m22(0).value() == 0);
    REQUIRE(m22(1).value() == 1);
    REQUIRE(m22(2).value() == 2);
    REQUIRE(m22(3).value() == 3);
    REQUIRE(!m22(4));

    power_of_2_bin_mapper<u32, u16, 2, 2, true> m22f;
    REQUIRE(m22f.get_n_bins() == 4);
    REQUIRE(m22f(0).value() == 3);
    REQUIRE(m22f(1).value() == 2);
    REQUIRE(m22f(2).value() == 1);
    REQUIRE(m22f(3).value() == 0);
    REQUIRE(!m22f(4));

    power_of_2_bin_mapper<u32, u16, 12, 8, false> m12_8;
    REQUIRE(m12_8.get_n_bins() == 256);
    REQUIRE(m12_8(0).value() == 0);
    REQUIRE(m12_8(15).value() == 0);
    REQUIRE(m12_8(16).value() == 1);
    REQUIRE(m12_8(4095).value() == 255);
    REQUIRE(!m12_8(4096));

    power_of_2_bin_mapper<u32, u16, 12, 8, true> m12_8f;
    REQUIRE(m12_8f.get_n_bins() == 256);
    REQUIRE(m12_8f(0).value() == 255);
    REQUIRE(m12_8f(15).value() == 255);
    REQUIRE(m12_8f(16).value() == 254);
    REQUIRE(m12_8f(4095).value() == 0);
    REQUIRE(!m12_8f(4096));

    power_of_2_bin_mapper<u32, u16, 16, 16, false> m16_16;
    REQUIRE(m16_16.get_n_bins() == 65536);
    REQUIRE(m16_16(0).value() == 0);
    REQUIRE(m16_16(1).value() == 1);
    REQUIRE(m16_16(65535).value() == 65535);

    power_of_2_bin_mapper<u16, u16, 16, 16, false> m16_16_16;
    REQUIRE(m16_16_16.get_n_bins() == 65536);
    REQUIRE(m16_16_16(0).value() == 0);
    REQUIRE(m16_16_16(1).value() == 1);
    REQUIRE(m16_16_16(65535).value() == 65535);

    power_of_2_bin_mapper<u32, u16, 32, 16, false> m32_16;
    REQUIRE(m32_16.get_n_bins() == 65536);
    REQUIRE(m32_16(0).value() == 0);
    REQUIRE(m32_16(65535).value() == 0);
    REQUIRE(m32_16(65536).value() == 1);
    auto m = std::numeric_limits<u32>::max();
    REQUIRE(m32_16(m - 65536).value() == 65534);
    REQUIRE(m32_16(m - 65535).value() == 65535);
    REQUIRE(m32_16(m).value() == 65535);
}

TEST_CASE("Linear bin mapping", "[linear_bin_mapper]") {
    bool clamp = GENERATE(false, true);
    auto checkClamped = [=](std::optional<u16> o, u16 clamped) {
        if (clamp)
            return o.value() == clamped;
        else
            return !o;
    };

    linear_bin_mapper<i32, u16> m010(0, 1, 0, clamp);
    REQUIRE(m010.get_n_bins() == 1);
    REQUIRE(checkClamped(m010(-1), 0));
    REQUIRE(m010(0).value() == 0);
    REQUIRE(checkClamped(m010(1), 0));

    linear_bin_mapper<i32, u16> m110(1, 1, 0, clamp);
    REQUIRE(m110.get_n_bins() == 1);
    REQUIRE(checkClamped(m110(0), 0));
    REQUIRE(m110(1).value() == 0);
    REQUIRE(checkClamped(m110(2), 0));

    linear_bin_mapper<i32, u16> nn10(-1, 1, 0, clamp);
    REQUIRE(nn10.get_n_bins() == 1);
    REQUIRE(checkClamped(nn10(-2), 0));
    REQUIRE(nn10(-1).value() == 0);
    REQUIRE(checkClamped(nn10(0), 0));

    linear_bin_mapper<i32, u16> m020(0, 2, 0, clamp);
    REQUIRE(m020.get_n_bins() == 1);
    REQUIRE(checkClamped(m020(-1), 0));
    REQUIRE(m020(0).value() == 0);
    REQUIRE(m020(1).value() == 0);
    REQUIRE(checkClamped(m020(2), 0));

    linear_bin_mapper<i32, u16> m120(1, 2, 0, clamp);
    REQUIRE(m120.get_n_bins() == 1);
    REQUIRE(checkClamped(m120(0), 0));
    REQUIRE(m120(1).value() == 0);
    REQUIRE(m120(2).value() == 0);
    REQUIRE(checkClamped(m120(3), 0));

    linear_bin_mapper<i32, u16> mn20(-1, 2, 0, clamp);
    REQUIRE(mn20.get_n_bins() == 1);
    REQUIRE(checkClamped(mn20(-2), 0));
    REQUIRE(mn20(-1).value() == 0);
    REQUIRE(mn20(0).value() == 0);
    REQUIRE(checkClamped(mn20(1), 0));

    linear_bin_mapper<i32, u16> m0n0(0, -1, 0, clamp);
    REQUIRE(m0n0.get_n_bins() == 1);
    REQUIRE(checkClamped(m0n0(1), 0));
    REQUIRE(m0n0(0).value() == 0);
    REQUIRE(checkClamped(m0n0(-1), 0));

    linear_bin_mapper<i32, u16> m1n0(1, -1, 0, clamp);
    REQUIRE(m1n0.get_n_bins() == 1);
    REQUIRE(checkClamped(m1n0(2), 0));
    REQUIRE(m1n0(1).value() == 0);
    REQUIRE(checkClamped(m1n0(0), 0));

    linear_bin_mapper<i32, u16> mnn0(-1, -1, 0, clamp);
    REQUIRE(mnn0.get_n_bins() == 1);
    REQUIRE(checkClamped(mnn0(0), 0));
    REQUIRE(mnn0(-1).value() == 0);
    REQUIRE(checkClamped(mnn0(-2), 0));

    linear_bin_mapper<i32, u16> m011(0, 1, 1, clamp);
    REQUIRE(m011.get_n_bins() == 2);
    REQUIRE(checkClamped(m011(-1), 0));
    REQUIRE(m011(0).value() == 0);
    REQUIRE(m011(1).value() == 1);
    REQUIRE(checkClamped(m011(2), 1));

    linear_bin_mapper<i32, u16> m111(1, 1, 1, clamp);
    REQUIRE(m111.get_n_bins() == 2);
    REQUIRE(checkClamped(m111(0), 0));
    REQUIRE(m111(1).value() == 0);
    REQUIRE(m111(2).value() == 1);
    REQUIRE(checkClamped(m111(3), 1));

    linear_bin_mapper<i32, u16> mn11(-1, 1, 1, clamp);
    REQUIRE(mn11.get_n_bins() == 2);
    REQUIRE(checkClamped(mn11(-2), 0));
    REQUIRE(mn11(-1).value() == 0);
    REQUIRE(mn11(0).value() == 1);
    REQUIRE(checkClamped(mn11(1), 1));

    linear_bin_mapper<i32, u16> m0n1(0, -1, 1, clamp);
    REQUIRE(m0n1.get_n_bins() == 2);
    REQUIRE(checkClamped(m0n1(1), 0));
    REQUIRE(m0n1(0).value() == 0);
    REQUIRE(m0n1(-1).value() == 1);
    REQUIRE(checkClamped(m0n1(-2), 1));

    linear_bin_mapper<i32, u16> m1n1(1, -1, 1, clamp);
    REQUIRE(m1n1.get_n_bins() == 2);
    REQUIRE(checkClamped(m1n1(2), 0));
    REQUIRE(m1n1(1).value() == 0);
    REQUIRE(m1n1(0).value() == 1);
    REQUIRE(checkClamped(m1n1(-1), 1));

    linear_bin_mapper<i32, u16> mnn1(-1, -1, 1, clamp);
    REQUIRE(mnn1.get_n_bins() == 2);
    REQUIRE(checkClamped(mnn1(0), 0));
    REQUIRE(mnn1(-1).value() == 0);
    REQUIRE(mnn1(-2).value() == 1);
    REQUIRE(checkClamped(mnn1(-3), 1));

    linear_bin_mapper<u32, u16> maxint(0, 32768, 65535, clamp);
    REQUIRE(maxint.get_n_bins() == 65536);
    REQUIRE(maxint(0).value() == 0);
    REQUIRE(maxint(32767).value() == 0);
    REQUIRE(maxint(32768).value() == 1);
    REQUIRE(maxint(std::numeric_limits<i32>::max()) == 65535);

    linear_bin_mapper<u32, u16> maxuint(0, 65536, 65535, clamp);
    REQUIRE(maxuint.get_n_bins() == 65536);
    REQUIRE(maxuint(0).value() == 0);
    REQUIRE(maxuint(65535).value() == 0);
    REQUIRE(maxuint(65536).value() == 1);
    REQUIRE(maxuint(std::numeric_limits<u32>::max()) == 65535);

    // Typical flipped 12-bit -> 8-bit
    linear_bin_mapper<i32, u16> flipped(4095, -16, 255, clamp);
    REQUIRE(flipped.get_n_bins() == 256);
    REQUIRE(flipped(0).value() == 255);
    REQUIRE(flipped(15).value() == 255);
    REQUIRE(flipped(16).value() == 254);
    REQUIRE(flipped(4095 - 16).value() == 1);
    REQUIRE(flipped(4095 - 15).value() == 0);
    REQUIRE(flipped(4095).value() == 0);
    REQUIRE(checkClamped(flipped(4096), 0));
    REQUIRE(checkClamped(flipped(65535), 0));
}

using Start = Event<0>;
using Stop = Event<1>;
using Other = Event<2>;
using BatchInput =
    event_set<bin_increment_event<unsigned>, Start, Stop, Other>;
using BatchOutput = event_set<bin_increment_batch_event<unsigned>, Other>;
using BatchOutVec = std::vector<event_variant<BatchOutput>>;

auto MakeBatchBinIncrementsFixture() {
    return MakeProcessorTestFixture<BatchInput, BatchOutput>(
        [](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return batch_bin_increments<unsigned, Start, Stop, D>(
                std::move(downstream));
        });
}

TEST_CASE("Batch bin increments", "[batch_bin_increments]") {
    auto f = MakeBatchBinIncrementsFixture();

    SECTION("Pass through unrelated") {
        f.FeedEvents({
            Other{42},
        });
        REQUIRE(f.Output() == BatchOutVec{
                                  Other{42},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == BatchOutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Stop before first start ignored") {
        f.FeedEvents({
            Stop{42},
        });
        REQUIRE(f.Output() == BatchOutVec{});
        f.FeedEnd({});
        REQUIRE(f.Output() == BatchOutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Start with no stop ignored") {
        f.FeedEvents({
            Start{42},
        });
        REQUIRE(f.Output() == BatchOutVec{});
        f.FeedEvents({
            bin_increment_event<unsigned>{43, 123},
        });
        REQUIRE(f.Output() == BatchOutVec{});
        f.FeedEnd({});
        REQUIRE(f.Output() == BatchOutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Events passed only between start and stop") {
        f.FeedEvents({
            Start{42},
        });
        REQUIRE(f.Output() == BatchOutVec{});
        f.FeedEvents({
            bin_increment_event<unsigned>{43, 123},
        });
        REQUIRE(f.Output() == BatchOutVec{});
        f.FeedEvents({
            Stop{44},
        });
        REQUIRE(f.Output() ==
                BatchOutVec{
                    bin_increment_batch_event<unsigned>{42, 44, {123}},
                });
        f.FeedEvents({
            Start{45},
        });
        REQUIRE(f.Output() == BatchOutVec{});
        f.FeedEvents({
            bin_increment_event<unsigned>{46, 124},
        });
        REQUIRE(f.Output() == BatchOutVec{});
        f.FeedEvents({
            bin_increment_event<unsigned>{47, 125},
        });
        REQUIRE(f.Output() == BatchOutVec{});
        f.FeedEvents({
            Stop{48},
        });
        REQUIRE(f.Output() ==
                BatchOutVec{
                    bin_increment_batch_event<unsigned>{45, 48, {124, 125}},
                });
    }
}
