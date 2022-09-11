/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/Histogram.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <catch2/catch.hpp>

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

using namespace flimevt;
using namespace flimevt::test;

using Reset = Event<0>;
using Other = Event<1>;
using Bins = EventSet<BinIncrementEvent<unsigned>, Reset, Other>;
using Batches = EventSet<BinIncrementBatchEvent<unsigned>, Reset, Other>;
using BatchesNoReset = EventSet<BinIncrementBatchEvent<unsigned>, Other>;
using Histos = EventSet<HistogramEvent<unsigned>,
                        AccumulatedHistogramEvent<unsigned>, Other>;
using HistosVec = std::vector<EventVariant<Histos>>;

template <typename Ovfl>
auto MakeHistogramFixture(std::size_t nBins, unsigned maxBin) {
    return MakeProcessorTestFixture<Bins, Histos>([=](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return Histogram<unsigned, unsigned, Reset, Ovfl, D>(
            nBins, maxBin, std::move(downstream));
    });
}

TEMPLATE_TEST_CASE("Histogram, zero bins", "[Histogram]", SaturateOnOverflow,
                   ResetOnOverflow, StopOnOverflow, ErrorOnOverflow) {
    auto f = MakeHistogramFixture<TestType>(0, 0);

    f.Feed(Other{42});
    REQUIRE(f.Check(Other{42}));

    f.Feed(Reset{});
    REQUIRE(f.Check(
        AccumulatedHistogramEvent<unsigned>{0, 0, {}, 0, 0, false, false}));

    f.FeedEnd({});
    REQUIRE(f.Check(
        AccumulatedHistogramEvent<unsigned>{0, 0, {}, 0, 0, false, true}));
    REQUIRE(f.DidEnd());
}

TEMPLATE_TEST_CASE("Histogram, no overflow", "[Histogram]", SaturateOnOverflow,
                   ResetOnOverflow, StopOnOverflow, ErrorOnOverflow) {
    auto f = MakeHistogramFixture<TestType>(2, 100);

    f.Feed(BinIncrementEvent<unsigned>{42, 0});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 42, {1, 0}, 1, 0}));

    f.Feed(BinIncrementEvent<unsigned>{43, 1});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1, 1}, 2, 0}));

    f.Feed(Reset{44});
    REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
        42, 43, {1, 1}, 2, 0, true, false}));

    f.Feed(BinIncrementEvent<unsigned>{45, 0});
    REQUIRE(f.Check(HistogramEvent<unsigned>{45, 45, {1, 0}, 1, 0}));

    f.FeedEnd({});
    REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
        45, 45, {1, 0}, 1, 0, true, true}));
    REQUIRE(f.DidEnd());
}

TEST_CASE("Histogram, saturate on overflow", "[Histogram]") {
    SECTION("Max per bin = 0") {
        auto f = MakeHistogramFixture<SaturateOnOverflow>(1, 0);

        f.Feed(BinIncrementEvent<unsigned>{42, 0}); // Overflow
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 42, {0}, 1, 1}));

        f.FeedEnd({});
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            42, 42, {0}, 1, 1, true, true}));
        REQUIRE(f.DidEnd());
    }

    SECTION("Max per bin = 1") {
        auto f = MakeHistogramFixture<SaturateOnOverflow>(1, 1);

        f.Feed(BinIncrementEvent<unsigned>{42, 0});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 42, {1}, 1, 0}));

        f.Feed(BinIncrementEvent<unsigned>{43, 0}); // Overflow
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1}, 2, 1}));

        f.Feed(Reset{44});
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            42, 43, {1}, 2, 1, true, false}));

        f.Feed(BinIncrementEvent<unsigned>{45, 0});
        REQUIRE(f.Check(HistogramEvent<unsigned>{45, 45, {1}, 1, 0}));

        f.FeedEnd({});
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            45, 45, {1}, 1, 0, true, true}));
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Histogram, reset on overflow", "[Histogram]") {
    SECTION("Max per bin = 0") {
        auto f = MakeHistogramFixture<ResetOnOverflow>(1, 0);

        f.Feed(BinIncrementEvent<unsigned>{42, 0}); // Overflow
        REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
    }

    SECTION("Max per bin = 1") {
        auto f = MakeHistogramFixture<ResetOnOverflow>(1, 1);

        f.Feed(BinIncrementEvent<unsigned>{42, 0});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 42, {1}, 1, 0}));

        f.Feed(BinIncrementEvent<unsigned>{43, 0}); // Overflow
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            42, 42, {1}, 1, 0, true, false}));
        REQUIRE(f.Check(HistogramEvent<unsigned>{43, 43, {1}, 1, 0}));

        f.FeedEnd({});
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            43, 43, {1}, 1, 0, true, true}));
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Histogram, stop on overflow", "[Histogram]") {
    SECTION("Max per bin = 0") {
        auto f = MakeHistogramFixture<StopOnOverflow>(1, 0);

        f.Feed(BinIncrementEvent<unsigned>{42, 0}); // Overflow
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            0, 0, {0}, 0, 0, false, true}));
        REQUIRE(f.DidEnd());
    }

    SECTION("Max per bin = 1") {
        auto f = MakeHistogramFixture<StopOnOverflow>(1, 1);

        f.Feed(BinIncrementEvent<unsigned>{42, 0});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 42, {1}, 1, 0}));

        f.Feed(BinIncrementEvent<unsigned>{43, 0}); // Overflow
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            42, 42, {1}, 1, 0, true, true}));
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Histogram, error on overflow", "[Histogram]") {
    SECTION("Max per bin = 0") {
        auto f = MakeHistogramFixture<ErrorOnOverflow>(1, 0);

        f.Feed(BinIncrementEvent<unsigned>{42, 0}); // Overflow
        REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
    }

    SECTION("Max per bin = 1") {
        auto f = MakeHistogramFixture<ErrorOnOverflow>(1, 1);

        f.Feed(BinIncrementEvent<unsigned>{42, 0});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 42, {1}, 1, 0}));

        f.Feed(BinIncrementEvent<unsigned>{43, 0}); // Overflow
        REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
    }
}

template <typename Ovfl>
auto MakeHistogramInBatchesFixture(std::size_t nBins, unsigned maxBin) {
    return MakeProcessorTestFixture<BatchesNoReset, Histos>(
        [=](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return HistogramInBatches<unsigned, unsigned, Ovfl, D>(
                nBins, maxBin, std::move(downstream));
        });
}

TEMPLATE_TEST_CASE("Histogram in batches, zero bins", "[HistogramInBatches]",
                   SaturateOnOverflow, ErrorOnOverflow) {
    auto f = MakeHistogramInBatchesFixture<TestType>(0, 0);

    f.Feed(Other{42});
    REQUIRE(f.Check(Other{42}));

    f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {}, 0, 0}));

    f.FeedEnd({});
    REQUIRE(f.DidEnd());
}

TEMPLATE_TEST_CASE("Histogram in batches, no overflow", "[HistogramInBatches]",
                   SaturateOnOverflow, ErrorOnOverflow) {
    auto f = MakeHistogramInBatchesFixture<TestType>(2, 100);

    f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1, 0}, 1, 0}));

    f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0, 1}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1, 1}, 2, 0}));

    f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {1, 0}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1, 1}, 2, 0}));

    f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {1, 1}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {0, 2}, 2, 0}));

    f.FeedEnd({});
    REQUIRE(f.DidEnd());
}

TEST_CASE("Histogram in batches, saturate on overflow",
          "[HistogramInBatches]") {
    SECTION("Max per bin = 0") {
        auto f = MakeHistogramInBatchesFixture<SaturateOnOverflow>(1, 0);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {0}, 1, 1}));

        f.FeedEnd({});
        REQUIRE(f.DidEnd());
    }

    SECTION("Max per bin = 1") {
        auto f = MakeHistogramInBatchesFixture<SaturateOnOverflow>(1, 1);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0, 0}}); // Overflow
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1}, 2, 1}));

        f.FeedEnd({});
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Histogram in batches, error on overflow", "[HistogramInBatches") {
    SECTION("Max per bin = 0") {
        auto f = MakeHistogramInBatchesFixture<ErrorOnOverflow>(1, 0);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
    }

    SECTION("Max per bin = 1") {
        auto f = MakeHistogramInBatchesFixture<ErrorOnOverflow>(1, 1);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0, 0}}); // Overflow
        REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
    }
}

template <typename Ovfl>
auto MakeAccumulateHistogramsFixture(std::size_t nBins, unsigned maxBin) {
    return MakeProcessorTestFixture<Batches, Histos>([=](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return AccumulateHistograms<unsigned, unsigned, Reset, Ovfl, D>(
            nBins, maxBin, std::move(downstream));
    });
}

TEMPLATE_TEST_CASE("Accumulate histograms, zero bins",
                   "[AccumulateHistograms]", SaturateOnOverflow,
                   ResetOnOverflow, StopOnOverflow, ErrorOnOverflow) {
    auto f = MakeAccumulateHistogramsFixture<TestType>(0, 0);

    f.Feed(Other{42});
    REQUIRE(f.Check(Other{42}));

    f.Feed(Reset{});
    REQUIRE(f.Check(
        AccumulatedHistogramEvent<unsigned>{0, 0, {}, 0, 0, false, false}));

    f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {}, 0, 0}));

    f.Feed(Reset{});
    REQUIRE(f.Check(
        AccumulatedHistogramEvent<unsigned>{42, 43, {}, 0, 0, true, false}));

    f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {}, 0, 0}));

    f.FeedEnd({});
    REQUIRE(f.Check(
        AccumulatedHistogramEvent<unsigned>{42, 43, {}, 0, 0, true, true}));
    REQUIRE(f.DidEnd());
}

TEMPLATE_TEST_CASE("Accumulate histograms, no overflow",
                   "[AccumulateHistograms]", SaturateOnOverflow,
                   ResetOnOverflow, StopOnOverflow, ErrorOnOverflow) {
    auto f = MakeAccumulateHistogramsFixture<TestType>(2, 100);

    f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1, 0}, 1, 0}));

    f.Feed(BinIncrementBatchEvent<unsigned>{44, 45, {0, 1}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{42, 45, {2, 1}, 3, 0}));

    f.Feed(Reset{46});
    REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
        42, 45, {2, 1}, 3, 0, true, false}));

    f.Feed(BinIncrementBatchEvent<unsigned>{47, 48, {1}});
    REQUIRE(f.Check(HistogramEvent<unsigned>{47, 48, {0, 1}, 1, 0}));

    f.FeedEnd({});
    REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
        47, 48, {0, 1}, 1, 0, true, true}));
    REQUIRE(f.DidEnd());
}

TEST_CASE("Accumulate histograms, saturate on overflow",
          "[AccumulateHistograms]") {
    SECTION("Max per bin = 0") {
        auto f = MakeAccumulateHistogramsFixture<SaturateOnOverflow>(1, 0);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {0}, 1, 1}));

        f.FeedEnd({});
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            42, 43, {0}, 1, 1, true, true}));
        REQUIRE(f.DidEnd());
    }

    SECTION("Max per bin = 1") {
        auto f = MakeAccumulateHistogramsFixture<SaturateOnOverflow>(1, 1);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1}, 1, 0}));

        f.Feed(BinIncrementBatchEvent<unsigned>{44, 45, {0}}); // Overflow
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 45, {1}, 2, 1}));

        f.Feed(Reset{46});
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            42, 45, {1}, 2, 1, true, false}));

        f.Feed(BinIncrementBatchEvent<unsigned>{47, 48, {0}});
        REQUIRE(f.Check(HistogramEvent<unsigned>{47, 48, {1}, 1, 0}));

        f.FeedEnd({});
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            47, 48, {1}, 1, 0, true, true}));
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Accumulate histograms, reset on overflow",
          "[AccumulateHistograms]") {
    SECTION("Max per bin = 0") {
        auto f = MakeAccumulateHistogramsFixture<ResetOnOverflow>(1, 0);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
    }

    SECTION("Max per bin = 1") {
        auto f = MakeAccumulateHistogramsFixture<ResetOnOverflow>(1, 1);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1}, 1, 0}));

        f.Feed(BinIncrementBatchEvent<unsigned>{44, 45, {0}}); // Overflow
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            42, 43, {1}, 1, 0, true, false}));
        REQUIRE(f.Check(HistogramEvent<unsigned>{44, 45, {1}, 1, 0}));

        SECTION("Normal end") {
            f.FeedEnd({});
            REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
                44, 45, {1}, 1, 0, true, true}));
            REQUIRE(f.DidEnd());
        }

        SECTION("Error on single-batch overflow") {
            f.Feed(
                BinIncrementBatchEvent<unsigned>{46, 47, {0, 0}}); // Overflow
            // Reset-before-overflow succeeds:
            REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
                44, 45, {1}, 1, 0, true, false}));
            // But the batch overflows by itself:
            REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
        }
    }

    SECTION("Roll back batch before resetting") {
        auto f = MakeAccumulateHistogramsFixture<ResetOnOverflow>(2, 1);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {1}});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {0, 1}, 1, 0}));

        SECTION("Successful reset") {
            f.Feed(
                BinIncrementBatchEvent<unsigned>{44, 45, {0, 1}}); // Overflow
            REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
                42, 43, {0, 1}, 1, 0, true, false}));
            REQUIRE(f.Check(HistogramEvent<unsigned>{44, 45, {1, 1}, 2, 0}));

            f.FeedEnd({});
            REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
                44, 45, {1, 1}, 2, 0, true, true}));
            REQUIRE(f.DidEnd());
        }

        SECTION("Error on single-batch overflow") {
            f.Feed(BinIncrementBatchEvent<unsigned>{
                44, 45, {0, 1, 1}}); // Overflow
            REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
                42, 43, {0, 1}, 1, 0, true, false}));
            REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
        }
    }
}

TEST_CASE("Accumulate histograms, stop on overflow",
          "[AccumulateHistograms]") {
    SECTION("Max per bin = 0") {
        auto f = MakeAccumulateHistogramsFixture<StopOnOverflow>(1, 0);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            0, 0, {0}, 0, 0, false, true}));
        REQUIRE(f.DidEnd());
    }

    SECTION("Max per bin = 1") {
        auto f = MakeAccumulateHistogramsFixture<StopOnOverflow>(1, 1);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1}, 1, 0}));

        f.Feed(BinIncrementBatchEvent<unsigned>{44, 45, {0}}); // Overflow
        REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
            42, 43, {1}, 1, 0, true, true}));
        REQUIRE(f.DidEnd());
    }

    SECTION("Roll back batch before stopping") {
        auto f = MakeAccumulateHistogramsFixture<StopOnOverflow>(2, 1);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {1}});
        REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {0, 1}, 1, 0}));

        SECTION("Overflow of accumulated") {
            f.Feed(
                BinIncrementBatchEvent<unsigned>{44, 45, {0, 1}}); // Overflow
            REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
                42, 43, {0, 1}, 1, 0, true, true}));
            REQUIRE(f.DidEnd());
        }

        SECTION("Single-batch overflow") {
            f.Feed(BinIncrementBatchEvent<unsigned>{
                44, 45, {0, 1, 1}}); // Overflow
            REQUIRE(f.Check(AccumulatedHistogramEvent<unsigned>{
                42, 43, {0, 1}, 1, 0, true, true}));
            REQUIRE(f.DidEnd());
        }
    }
}

TEST_CASE("Accumulate histograms, error on overflow",
          "[AccumulateHistograms]") {
    SECTION("Max per bin = 0") {
        auto f = MakeAccumulateHistogramsFixture<ErrorOnOverflow>(1, 0);

        f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}}); // Overflow
        REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
    }

    SECTION("Max per bin = 1") {
        auto f = MakeAccumulateHistogramsFixture<ErrorOnOverflow>(1, 1);

        SECTION("Overflow of accumulated") {
            f.Feed(BinIncrementBatchEvent<unsigned>{42, 43, {0}});
            REQUIRE(f.Check(HistogramEvent<unsigned>{42, 43, {1}, 1, 0}));

            f.Feed(BinIncrementBatchEvent<unsigned>{44, 45, {0}}); // Overflow
            REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
        }

        SECTION("Single-batch overflow") {
            f.Feed(
                BinIncrementBatchEvent<unsigned>{44, 45, {0, 0}}); // Overflow
            REQUIRE_THROWS_AS(f.DidEnd(), HistogramOverflowError);
        }
    }
}
