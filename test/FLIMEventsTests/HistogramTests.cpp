#include "FLIMEvents/Histogram.hpp"
#include <catch2/catch.hpp>

TEST_CASE("TimeBins", "[Histogram]") {
    Histogram<uint16_t> hist(8, 12, false, 1, 1);
    auto data = hist.Get();
    hist.Clear();

    hist.Increment(0, 0, 0);
    REQUIRE(data[0] == 1);
    hist.Increment(15, 0, 0);
    REQUIRE(data[0] == 2);
    hist.Increment(16, 0, 0);
    REQUIRE(data[1] == 1);

    hist.Increment(4095, 0, 0);
    REQUIRE(data[255] == 1);
    hist.Increment(4080, 0, 0);
    REQUIRE(data[255] == 2);
    hist.Increment(4079, 0, 0);
    REQUIRE(data[254] == 1);
}

TEST_CASE("ReverseTimeBins", "[Histogram]") {
    Histogram<uint16_t> hist(8, 12, true, 1, 1);
    auto data = hist.Get();
    hist.Clear();

    hist.Increment(0, 0, 0);
    REQUIRE(data[255] == 1);
    hist.Increment(15, 0, 0);
    REQUIRE(data[255] == 2);
    hist.Increment(16, 0, 0);
    REQUIRE(data[254] == 1);

    hist.Increment(4095, 0, 0);
    REQUIRE(data[0] == 1);
    hist.Increment(4080, 0, 0);
    REQUIRE(data[0] == 2);
    hist.Increment(4079, 0, 0);
    REQUIRE(data[1] == 1);
}

TEST_CASE("SingleTimeBin", "[Histogram]") {
    SECTION("Non-reversed") {
        Histogram<uint16_t> hist(0, 7, false, 1, 1);
        auto data = hist.Get();
        hist.Clear();

        hist.Increment(0, 0, 0);
        REQUIRE(data[0] == 1);
        hist.Increment(127, 0, 0);
        REQUIRE(data[0] == 2);
    }

    SECTION("Reversed") {
        Histogram<uint16_t> hist(0, 7, false, 1, 1);
        auto data = hist.Get();
        hist.Clear();

        hist.Increment(0, 0, 0);
        REQUIRE(data[0] == 1);
        hist.Increment(127, 0, 0);
        REQUIRE(data[0] == 2);
    }
}
