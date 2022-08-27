#include "FLIMEvents/Common.hpp"

#include <catch2/catch.hpp>

TEST_CASE("CountTrailingZeros32", "[Common]") {
    using namespace flimevt::internal;

    REQUIRE(CountTrailingZeros32(1) == 0);
    REQUIRE(CountTrailingZeros32Nonintrinsic(1) == 0);

    REQUIRE(CountTrailingZeros32(2) == 1);
    REQUIRE(CountTrailingZeros32Nonintrinsic(2) == 1);

    REQUIRE(CountTrailingZeros32(3) == 0);
    REQUIRE(CountTrailingZeros32Nonintrinsic(3) == 0);

    REQUIRE(CountTrailingZeros32(4) == 2);
    REQUIRE(CountTrailingZeros32Nonintrinsic(4) == 2);

    REQUIRE(CountTrailingZeros32(6) == 1);
    REQUIRE(CountTrailingZeros32Nonintrinsic(6) == 1);

    REQUIRE(CountTrailingZeros32(1U << 31) == 31);
    REQUIRE(CountTrailingZeros32Nonintrinsic(1U << 31) == 31);
}
