#include <catch2/catch.hpp>
#include "FLIMEvents/BHDeviceEvent.hpp"


TEST_CASE("ADCValue", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        uint8_t bytes[4];
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
        uint8_t bytes[4];
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


TEST_CASE("MacroTime", "[BHSPCEvent]") {
    REQUIRE(BHSPCEvent::MacroTimeOverflowPeriod == 4096);

    union {
        BHSPCEvent event;
        uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.GetMacroTime() == 0);

    u.bytes[0] = 0xff;
    REQUIRE(u.event.GetMacroTime() == 0xff);

    u.bytes[1] = 0x0f;
    REQUIRE(u.event.GetMacroTime() == 4095);

    u.bytes[0] = 0;
    REQUIRE(u.event.GetMacroTime() == 0xf00);

    u.bytes[1] = 0xf0;
    u.bytes[2] = 0xff;
    u.bytes[3] = 0xff;
    REQUIRE(u.event.GetMacroTime() == 0);
}


TEST_CASE("Flags", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(!u.event.GetInvalidFlag());
    REQUIRE(!u.event.GetMacroTimeOverflowFlag());
    REQUIRE(!u.event.GetGapFlag());
    REQUIRE(!u.event.GetMarkerFlag());

    u.bytes[3] = 1 << 7;
    REQUIRE(u.event.GetInvalidFlag());
    u.bytes[3] = 1 << 6;
    REQUIRE(u.event.GetMacroTimeOverflowFlag());
    u.bytes[3] = 1 << 5;
    REQUIRE(u.event.GetGapFlag());
    u.bytes[3] = 1 << 4;
    REQUIRE(u.event.GetMarkerFlag());
}


TEST_CASE("MacroTimeOverflow", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    // The GAP flag is orthogonal to macro-time overflow. Test all combinations
    // of the other 3 flags. (Although it is expected that INVALID is always
    // set when MARK is set.)
    uint8_t const INVALID = 1 << 7;
    uint8_t const MTOV = 1 << 6;
    uint8_t const MARK = 1 << 4;

    u.bytes[3] = 0; // Valid photon, no overflow
    REQUIRE(!u.event.IsMultipleMacroTimeOverflow());
    u.bytes[3] = MARK; // Mark, no overflow (not expected)
    REQUIRE(!u.event.IsMultipleMacroTimeOverflow());
    u.bytes[3] = MTOV; // Valid photon, single overflow
    REQUIRE(!u.event.IsMultipleMacroTimeOverflow());
    u.bytes[3] = MTOV | MARK; // Marker, single overflow (not expected)
    REQUIRE(!u.event.IsMultipleMacroTimeOverflow());
    u.bytes[3] = INVALID; // Invalid photon, no overflow
    REQUIRE(!u.event.IsMultipleMacroTimeOverflow());
    u.bytes[3] = INVALID | MARK; // Mark, no overflow
    REQUIRE(!u.event.IsMultipleMacroTimeOverflow());
    u.bytes[3] = INVALID | MTOV; // Multiple overflow
    REQUIRE(u.event.IsMultipleMacroTimeOverflow());
    u.bytes[3] = INVALID | MTOV | MARK; // Marker, single overflow
    REQUIRE(!u.event.IsMultipleMacroTimeOverflow());
}


TEST_CASE("MacroTimeOverflowCount", "[BHSPCEvent]") {
    union {
        BHSPCEvent event;
        uint8_t bytes[4];
    } u;
    memset(u.bytes, 0, 4);

    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 0);

    u.bytes[0] = 1;
    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 1);
    u.bytes[0] = 0x80;
    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 128);
    u.bytes[0] = 0;

    u.bytes[1] = 1;
    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 256);
    u.bytes[1] = 0x80;
    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 32768);
    u.bytes[1] = 0;

    u.bytes[2] = 1;
    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 65536);
    u.bytes[2] = 0x80;
    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 8388608);
    u.bytes[2] = 0;

    u.bytes[3] = 1;
    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 16777216);
    u.bytes[3] = 0x08;
    REQUIRE(u.event.GetMultipleMacroTimeOverflowCount() == 134217728);
    u.bytes[3] = 0;
}
