#include <catch2/catch.hpp>
#include "FLIMEvents/PixelPhotonEvent.hpp"


TEST_CASE("Frames are produced according to line markers", "[LineClockPixellator]") {
    // We could use a mocking framework (e.g. Trompeloeil), but this is simple
    // enough to do manually.
    class MockProcessor : public PixelPhotonProcessor {
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

        void HandleBeginFrame() override {
            ++beginFrameCount;
        }

        void HandleEndFrame() override {
            ++endFrameCount;
        }

        void HandlePixelPhoton(PixelPhotonEvent const& event) override {
            pixelPhotons.emplace_back(event);
        }

        void HandleError(std::string const& message) override {
            errors.emplace_back(message);
        }

        void HandleFinish() override {
            ++finishCount;
        }
    };

    auto output = std::make_shared<MockProcessor>();
    output->Reset();

    SECTION("2x2 frames with no photons") {
        auto lcp = std::make_shared<LineClockPixellator>(2, 2, 10, 0, 20, output);

        MarkerEvent lineMarker;
        lineMarker.bits = 1 << 1;
        lineMarker.macrotime = 100;
        lcp->HandleMarker(lineMarker);
        lcp->Flush();

        REQUIRE(output->beginFrameCount == 1);
        output->Reset();

        lineMarker.macrotime = 200;
        lcp->HandleMarker(lineMarker);
        lcp->Flush();
        REQUIRE(output->beginFrameCount == 0);
        REQUIRE(output->endFrameCount == 0);
        output->Reset();

        lineMarker.macrotime = 300;
        lcp->HandleMarker(lineMarker);
        lcp->Flush();
        REQUIRE(output->beginFrameCount == 1);
        REQUIRE(output->endFrameCount == 1);
        output->Reset();

        SECTION("Last frame is incomplete if last line not started") {
            DecodedEvent timestamp;
            timestamp.macrotime = 1000000;
            lcp->HandleTimestamp(timestamp);
            lcp->Flush();
            REQUIRE(output->beginFrameCount == 0);
            REQUIRE(output->endFrameCount == 0);
            output->Reset();
        }

        SECTION("Last frame completion detected by last seen timestamp") {
            lineMarker.macrotime = 400;
            lcp->HandleMarker(lineMarker);
            lcp->Flush();
            REQUIRE(output->beginFrameCount == 0);
            REQUIRE(output->endFrameCount == 0);
            output->Reset();

            DecodedEvent timestamp;
            timestamp.macrotime = 419;
            lcp->HandleTimestamp(timestamp);
            lcp->Flush();
            REQUIRE(output->beginFrameCount == 0);
            REQUIRE(output->endFrameCount == 0);
            output->Reset();

            timestamp.macrotime = 420;
            lcp->HandleTimestamp(timestamp);
            lcp->Flush();
            REQUIRE(output->beginFrameCount == 0);
            REQUIRE(output->endFrameCount == 1);
            output->Reset();
        }
    }

    SECTION("Photon placed correctly in 2x1 frame") {
        // Delay = 5, time = 25, so pixels range over times [5, 15) and [15,
        // 25) relative to the (single) line marker.
        auto lcp = std::make_shared<LineClockPixellator>(2, 1, 1, 5, 20, output);

        MarkerEvent lineMarker;
        lineMarker.bits = 1 << 1;
        lineMarker.macrotime = 100;
        lcp->HandleMarker(lineMarker);
        lcp->Flush();

        ValidPhotonEvent photon;
        memset(&photon, 0, sizeof(photon));

        for (auto mt : { 104, 105, 114, 115, 124, 125 }) {
            photon.macrotime = mt;
            lcp->HandleValidPhoton(photon);
        }

        lcp->Flush();
        REQUIRE(output->beginFrameCount == 1);
        REQUIRE(output->endFrameCount == 1);
        REQUIRE(output->pixelPhotons.size() == 4);
        REQUIRE(output->pixelPhotons[0].x == 0);
        REQUIRE(output->pixelPhotons[1].x == 0);
        REQUIRE(output->pixelPhotons[2].x == 1);
        REQUIRE(output->pixelPhotons[3].x == 1);
    }

    // TODO Other things we might test
    // - 1x1 frame size edge case
    // - photons between lines discarded
    // - large line delay compared to line interval (with/without photons)
    // - large negative line delay compared to line interval (with/without photons)
    //   - in particular, line spanning negative time
}
