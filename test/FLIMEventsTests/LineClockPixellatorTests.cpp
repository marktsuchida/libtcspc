#include "FLIMEvents/LineClockPixellator.hpp"

#include "FLIMEvents/DynamicPolymorphism.hpp"

#include <catch2/catch.hpp>

#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace flimevt;

TEST_CASE("Frames are produced according to line markers",
          "[LineClockPixellator]") {
    // We could use a mocking framework (e.g. Trompeloeil), but this is simple
    // enough to do manually.
    class MockProcessor {
      public:
        unsigned beginFrameCount;
        unsigned endFrameCount;
        std::vector<PixelPhotonEvent> pixelPhotons;
        std::vector<std::string> errors;
        unsigned finishCount;

        void Reset() {
            beginFrameCount = 0;
            endFrameCount = 0;
            pixelPhotons.clear();
            errors.clear();
            finishCount = 0;
        }

        void HandleEvent(BeginFrameEvent const &) { ++beginFrameCount; }

        void HandleEvent(EndFrameEvent const &) { ++endFrameCount; }

        void HandleEvent(PixelPhotonEvent const &event) {
            pixelPhotons.emplace_back(event);
        }

        void HandleEnd(std::exception_ptr error) {
            if (error) {
                try {
                    std::rethrow_exception(error);
                } catch (std::exception const &e) {
                    errors.emplace_back(e.what());
                }
            } else {
                ++finishCount;
            }
        }
    };

    using VirtualMockProcessor =
        VirtualWrappedProcessor<MockProcessor, PixelPhotonEvents>;

    auto sharedOutput = std::make_shared<VirtualMockProcessor>();
    auto &output = sharedOutput->Wrapped();
    output.Reset();

    SECTION("2x2 frames with no photons") {
        PolymorphicProcessor<PixelPhotonEvents> polymorphic(sharedOutput);

        LineClockPixellator<decltype(polymorphic)> lcp(2, 2, 10, 0, 20, 1,
                                                       std::move(polymorphic));

        MarkerEvent lineMarker;
        lineMarker.bits = 1 << 1;
        lineMarker.macrotime = 100;
        lcp.HandleEvent(lineMarker);
        lcp.Flush();

        REQUIRE(output.beginFrameCount == 1);
        output.Reset();

        lineMarker.macrotime = 200;
        lcp.HandleEvent(lineMarker);
        lcp.Flush();
        REQUIRE(output.beginFrameCount == 0);
        REQUIRE(output.endFrameCount == 0);
        output.Reset();

        lineMarker.macrotime = 300;
        lcp.HandleEvent(lineMarker);
        lcp.Flush();
        REQUIRE(output.beginFrameCount == 1);
        REQUIRE(output.endFrameCount == 1);
        output.Reset();

        SECTION("Last frame is incomplete if last line not started") {
            TimestampEvent timestamp;
            timestamp.macrotime = 1000000;
            lcp.HandleEvent(timestamp);
            lcp.Flush();
            REQUIRE(output.beginFrameCount == 0);
            REQUIRE(output.endFrameCount == 0);
            output.Reset();
        }

        SECTION("Last frame completion detected by last seen timestamp") {
            lineMarker.macrotime = 400;
            lcp.HandleEvent(lineMarker);
            lcp.Flush();
            REQUIRE(output.beginFrameCount == 0);
            REQUIRE(output.endFrameCount == 0);
            output.Reset();

            TimestampEvent timestamp;
            timestamp.macrotime = 419;
            lcp.HandleEvent(timestamp);
            lcp.Flush();
            REQUIRE(output.beginFrameCount == 0);
            REQUIRE(output.endFrameCount == 0);
            output.Reset();

            timestamp.macrotime = 420;
            lcp.HandleEvent(timestamp);
            lcp.Flush();
            REQUIRE(output.beginFrameCount == 0);
            REQUIRE(output.endFrameCount == 1);
            output.Reset();
        }
    }

    SECTION("Photon placed correctly in 2x1 frame") {
        PolymorphicProcessor<PixelPhotonEvents> polymorphic(sharedOutput);

        // Delay = 5, time = 25, so pixels range over times [5, 15) and [15,
        // 25) relative to the (single) line marker.
        LineClockPixellator<decltype(polymorphic)> lcp(2, 1, 1, 5, 20, 1,
                                                       std::move(polymorphic));

        MarkerEvent lineMarker;
        lineMarker.bits = 1 << 1;
        lineMarker.macrotime = 100;
        lcp.HandleEvent(lineMarker);
        lcp.Flush();

        ValidPhotonEvent photon;
        memset(&photon, 0, sizeof(photon));

        for (auto mt : {104, 105, 114, 115, 124, 125}) {
            photon.macrotime = mt;
            lcp.HandleEvent(photon);
        }

        lcp.Flush();
        REQUIRE(output.beginFrameCount == 1);
        REQUIRE(output.endFrameCount == 1);
        REQUIRE(output.pixelPhotons.size() == 4);
        REQUIRE(output.pixelPhotons[0].x == 0);
        REQUIRE(output.pixelPhotons[1].x == 0);
        REQUIRE(output.pixelPhotons[2].x == 1);
        REQUIRE(output.pixelPhotons[3].x == 1);
    }

    // TODO Other things we might test
    // - 1x1 frame size edge case
    // - photons between lines discarded
    // - large line delay compared to line interval (with/without photons)
    // - large negative line delay compared to line interval (with/without
    // photons)
    //   - in particular, line spanning negative time
}
