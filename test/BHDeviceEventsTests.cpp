#include "FLIMEvents/BHDeviceEvents.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/NoopProcessor.hpp"

#include <catch2/catch.hpp>

#include <cstring>

using namespace flimevt;

using TCSPCNoop = NoopProcessor<TCSPCEvents>;

static_assert(HandlesEventSetV<BHSPCEventDecoder<TCSPCNoop>, BHSPCEvents>);
static_assert(
    HandlesEventSetV<BHSPC600Event48Decoder<TCSPCNoop>, BHSPC600Events48>);
static_assert(
    HandlesEventSetV<BHSPC600Event32Decoder<TCSPCNoop>, BHSPC600Events32>);

TEST_CASE("ADCValue", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.GetADCValue() == 0);

    u.bytes[2] = 0xff;
    REQUIRE(u.event.GetADCValue() == 0xff);

    u.bytes[3] = 0x0f;
    REQUIRE(u.event.GetADCValue() == 4095);

    u.bytes[2] = 0;
    REQUIRE(u.event.GetADCValue() == 0xf00);

    u.bytes[0] = 0xff;
    u.bytes[1] = 0xff;
    u.bytes[3] = 0xf0;
    REQUIRE(u.event.GetADCValue() == 0);
}

TEST_CASE("RoutingSignals", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.GetRoutingSignals() == 0);
    REQUIRE(u.event.GetMarkerBits() == 0);

    u.bytes[1] = 0x10;
    REQUIRE(u.event.GetRoutingSignals() == 1);
    REQUIRE(u.event.GetMarkerBits() == 1);
    u.bytes[1] = 0x20;
    REQUIRE(u.event.GetRoutingSignals() == 2);
    REQUIRE(u.event.GetMarkerBits() == 2);
    u.bytes[1] = 0x40;
    REQUIRE(u.event.GetRoutingSignals() == 4);
    REQUIRE(u.event.GetMarkerBits() == 4);
    u.bytes[1] = 0x80;
    REQUIRE(u.event.GetRoutingSignals() == 8);
    REQUIRE(u.event.GetMarkerBits() == 8);

    u.bytes[0] = u.bytes[2] = u.bytes[3] = 0xff;
    u.bytes[1] = 0x0f;
    REQUIRE(u.event.GetRoutingSignals() == 0);
    REQUIRE(u.event.GetMarkerBits() == 0);
}

TEST_CASE("Macrotime", "[BHSPCEvent]") {
    REQUIRE(BHSPCEvent::MacrotimeOverflowPeriod == 4096);

    union {
        BHSPCEvent event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.GetMacrotime() == 0);

    u.bytes[0] = 0xff;
    REQUIRE(u.event.GetMacrotime() == 0xff);

    u.bytes[1] = 0x0f;
    REQUIRE(u.event.GetMacrotime() == 4095);

    u.bytes[0] = 0;
    REQUIRE(u.event.GetMacrotime() == 0xf00);

    u.bytes[1] = 0xf0;
    u.bytes[2] = 0xff;
    u.bytes[3] = 0xff;
    REQUIRE(u.event.GetMacrotime() == 0);
}

TEST_CASE("Flags", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(!u.event.GetInvalidFlag());
    REQUIRE(!u.event.GetMacrotimeOverflowFlag());
    REQUIRE(!u.event.GetGapFlag());
    REQUIRE(!u.event.GetMarkerFlag());

    u.bytes[3] = 1 << 7;
    REQUIRE(u.event.GetInvalidFlag());
    u.bytes[3] = 1 << 6;
    REQUIRE(u.event.GetMacrotimeOverflowFlag());
    u.bytes[3] = 1 << 5;
    REQUIRE(u.event.GetGapFlag());
    u.bytes[3] = 1 << 4;
    REQUIRE(u.event.GetMarkerFlag());
}

TEST_CASE("MacrotimeOverflow", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    // The GAP flag is orthogonal to macrotime overflow. Test all combinations
    // of the other 3 flags. (Although it is expected that INVALID is always
    // set when MARK is set.)
    std::uint8_t const INVALID = 1 << 7;
    std::uint8_t const MTOV = 1 << 6;
    std::uint8_t const MARK = 1 << 4;

    u.bytes[3] = 0; // Valid photon, no overflow
    REQUIRE(!u.event.IsMultipleMacrotimeOverflow());
    u.bytes[3] = MARK; // Mark, no overflow (not expected)
    REQUIRE(!u.event.IsMultipleMacrotimeOverflow());
    u.bytes[3] = MTOV; // Valid photon, single overflow
    REQUIRE(!u.event.IsMultipleMacrotimeOverflow());
    u.bytes[3] = MTOV | MARK; // Marker, single overflow (not expected)
    REQUIRE(!u.event.IsMultipleMacrotimeOverflow());
    u.bytes[3] = INVALID; // Invalid photon, no overflow
    REQUIRE(!u.event.IsMultipleMacrotimeOverflow());
    u.bytes[3] = INVALID | MARK; // Mark, no overflow
    REQUIRE(!u.event.IsMultipleMacrotimeOverflow());
    u.bytes[3] = INVALID | MTOV; // Multiple overflow
    REQUIRE(u.event.IsMultipleMacrotimeOverflow());
    u.bytes[3] = INVALID | MTOV | MARK; // Marker, single overflow
    REQUIRE(!u.event.IsMultipleMacrotimeOverflow());
}

TEST_CASE("MacrotimeOverflowCount", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        std::uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 0);

    u.bytes[0] = 1;
    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 1);
    u.bytes[0] = 0x80;
    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 128);
    u.bytes[0] = 0;

    u.bytes[1] = 1;
    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 256);
    u.bytes[1] = 0x80;
    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 32768);
    u.bytes[1] = 0;

    u.bytes[2] = 1;
    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 65536);
    u.bytes[2] = 0x80;
    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 8388608);
    u.bytes[2] = 0;

    u.bytes[3] = 1;
    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 16777216);
    u.bytes[3] = 0x08;
    REQUIRE(u.event.GetMultipleMacrotimeOverflowCount() == 134217728);
    u.bytes[3] = 0;
}
