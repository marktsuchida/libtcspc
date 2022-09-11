/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/LineClockPixellator.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/DynamicPolymorphism.hpp"
#include "FLIMEvents/EventSet.hpp"

#include <catch2/catch.hpp>

#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace flimevt;

static_assert(handles_event_set_v<
              line_clock_pixellator<discard_all<pixel_photon_events>>,
              tcspc_events>);

TEST_CASE("Frames are produced according to line markers",
          "[line_clock_pixellator]") {
    // We could use a mocking framework (e.g. Trompeloeil), but this is simple
    // enough to do manually.
    class MockProcessor {
      public:
        unsigned beginFrameCount;
        unsigned endFrameCount;
        std::vector<pixel_photon_event> pixelPhotons;
        std::vector<std::string> errors;
        unsigned finishCount;

        void Reset() {
            beginFrameCount = 0;
            endFrameCount = 0;
            pixelPhotons.clear();
            errors.clear();
            finishCount = 0;
        }

        void handle_event(begin_frame_event const &) { ++beginFrameCount; }

        void handle_event(end_frame_event const &) { ++endFrameCount; }

        void handle_event(pixel_photon_event const &event) {
            pixelPhotons.emplace_back(event);
        }

        void handle_end(std::exception_ptr error) {
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
        virtual_wrapped_processor<MockProcessor, pixel_photon_events>;

    auto sharedOutput = std::make_shared<VirtualMockProcessor>();
    auto &output = sharedOutput->wrapped();
    output.Reset();

    SECTION("2x2 frames with no photons") {
        polymorphic_processor<pixel_photon_events> polymorphic(sharedOutput);

        line_clock_pixellator<decltype(polymorphic)> lcp(
            2, 2, 10, 0, 20, 1, std::move(polymorphic));

        marker_event lineMarker;
        lineMarker.channel = 1;
        lineMarker.macrotime = 100;
        lcp.handle_event(lineMarker);
        lcp.flush();

        REQUIRE(output.beginFrameCount == 1);
        output.Reset();

        lineMarker.macrotime = 200;
        lcp.handle_event(lineMarker);
        lcp.flush();
        REQUIRE(output.beginFrameCount == 0);
        REQUIRE(output.endFrameCount == 0);
        output.Reset();

        lineMarker.macrotime = 300;
        lcp.handle_event(lineMarker);
        lcp.flush();
        REQUIRE(output.beginFrameCount == 1);
        REQUIRE(output.endFrameCount == 1);
        output.Reset();

        SECTION("Last frame is incomplete if last line not started") {
            time_reached_event timestamp;
            timestamp.macrotime = 1000000;
            lcp.handle_event(timestamp);
            lcp.flush();
            REQUIRE(output.beginFrameCount == 0);
            REQUIRE(output.endFrameCount == 0);
            output.Reset();
        }

        SECTION("Last frame completion detected by last seen timestamp") {
            lineMarker.macrotime = 400;
            lcp.handle_event(lineMarker);
            lcp.flush();
            REQUIRE(output.beginFrameCount == 0);
            REQUIRE(output.endFrameCount == 0);
            output.Reset();

            time_reached_event timestamp;
            timestamp.macrotime = 419;
            lcp.handle_event(timestamp);
            lcp.flush();
            REQUIRE(output.beginFrameCount == 0);
            REQUIRE(output.endFrameCount == 0);
            output.Reset();

            timestamp.macrotime = 420;
            lcp.handle_event(timestamp);
            lcp.flush();
            REQUIRE(output.beginFrameCount == 0);
            REQUIRE(output.endFrameCount == 1);
            output.Reset();
        }
    }

    SECTION("Photon placed correctly in 2x1 frame") {
        polymorphic_processor<pixel_photon_events> polymorphic(sharedOutput);

        // Delay = 5, time = 25, so pixels range over times [5, 15) and [15,
        // 25) relative to the (single) line marker.
        line_clock_pixellator<decltype(polymorphic)> lcp(
            2, 1, 1, 5, 20, 1, std::move(polymorphic));

        marker_event lineMarker;
        lineMarker.channel = 1;
        lineMarker.macrotime = 100;
        lcp.handle_event(lineMarker);
        lcp.flush();

        time_correlated_count_event photon;
        memset(&photon, 0, sizeof(photon));

        for (auto mt : {104, 105, 114, 115, 124, 125}) {
            photon.macrotime = mt;
            lcp.handle_event(photon);
        }

        lcp.flush();
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
