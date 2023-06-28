/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/line_clock_pixellator.hpp"

#include "libtcspc/discard.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/ref_processor.hpp"

#include <catch2/catch_all.hpp>

#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

// NOLINTBEGIN

using namespace tcspc;

static_assert(handles_event_set_v<
              line_clock_pixellator<discard_all<pixel_photon_events>>,
              tcspc_events>);

TEST_CASE("Frames are produced according to line markers",
          "[line_clock_pixellator]") {
    // We could use a mocking framework (e.g. Trompeloeil), but this is simple
    // enough to do manually.
    class mock_processor {
      public:
        unsigned begin_frame_count;
        unsigned end_frame_count;
        std::vector<pixel_photon_event> pixel_photons;
        std::vector<std::string> errors;
        unsigned finish_count;

        void reset() {
            begin_frame_count = 0;
            end_frame_count = 0;
            pixel_photons.clear();
            errors.clear();
            finish_count = 0;
        }

        void handle_event(begin_frame_event const &) { ++begin_frame_count; }

        void handle_event(end_frame_event const &) { ++end_frame_count; }

        void handle_event(pixel_photon_event const &event) {
            pixel_photons.emplace_back(event);
        }

        void handle_end(std::exception_ptr const &error) {
            if (error) {
                try {
                    std::rethrow_exception(error);
                } catch (std::exception const &e) {
                    errors.emplace_back(e.what());
                }
            } else {
                ++finish_count;
            }
        }
    };

    mock_processor output;
    auto ref_output = ref_processor(output);
    output.reset();

    SECTION("2x2 frames with no photons") {
        line_clock_pixellator<decltype(ref_output)> lcp(2, 2, 10, 0, 20, 1,
                                                        std::move(ref_output));

        marker_event line_marker;
        line_marker.channel = 1;
        line_marker.macrotime = 100;
        lcp.handle_event(line_marker);
        lcp.flush();

        REQUIRE(output.begin_frame_count == 1);
        output.reset();

        line_marker.macrotime = 200;
        lcp.handle_event(line_marker);
        lcp.flush();
        REQUIRE(output.begin_frame_count == 0);
        REQUIRE(output.end_frame_count == 0);
        output.reset();

        line_marker.macrotime = 300;
        lcp.handle_event(line_marker);
        lcp.flush();
        REQUIRE(output.begin_frame_count == 1);
        REQUIRE(output.end_frame_count == 1);
        output.reset();

        SECTION("Last frame is incomplete if last line not started") {
            time_reached_event timestamp;
            timestamp.macrotime = 1000000;
            lcp.handle_event(timestamp);
            lcp.flush();
            REQUIRE(output.begin_frame_count == 0);
            REQUIRE(output.end_frame_count == 0);
            output.reset();
        }

        SECTION("Last frame completion detected by last seen timestamp") {
            line_marker.macrotime = 400;
            lcp.handle_event(line_marker);
            lcp.flush();
            REQUIRE(output.begin_frame_count == 0);
            REQUIRE(output.end_frame_count == 0);
            output.reset();

            time_reached_event timestamp;
            timestamp.macrotime = 419;
            lcp.handle_event(timestamp);
            lcp.flush();
            REQUIRE(output.begin_frame_count == 0);
            REQUIRE(output.end_frame_count == 0);
            output.reset();

            timestamp.macrotime = 420;
            lcp.handle_event(timestamp);
            lcp.flush();
            REQUIRE(output.begin_frame_count == 0);
            REQUIRE(output.end_frame_count == 1);
            output.reset();
        }
    }

    SECTION("Photon placed correctly in 2x1 frame") {
        // Delay = 5, time = 25, so pixels range over times [5, 15) and [15,
        // 25) relative to the (single) line marker.
        line_clock_pixellator<decltype(ref_output)> lcp(2, 1, 1, 5, 20, 1,
                                                        std::move(ref_output));

        marker_event line_marker;
        line_marker.channel = 1;
        line_marker.macrotime = 100;
        lcp.handle_event(line_marker);
        lcp.flush();

        time_correlated_count_event photon;
        memset(&photon, 0, sizeof(photon));

        for (auto mt : {104, 105, 114, 115, 124, 125}) {
            photon.macrotime = mt;
            lcp.handle_event(photon);
        }

        lcp.flush();
        REQUIRE(output.begin_frame_count == 1);
        REQUIRE(output.end_frame_count == 1);
        REQUIRE(output.pixel_photons.size() == 4);
        REQUIRE(output.pixel_photons[0].x == 0);
        REQUIRE(output.pixel_photons[1].x == 0);
        REQUIRE(output.pixel_photons[2].x == 1);
        REQUIRE(output.pixel_photons[3].x == 1);
    }

    // TODO Other things we might test
    // - 1x1 frame size edge case
    // - photons between lines discarded
    // - large line delay compared to line interval (with/without photons)
    // - large negative line delay compared to line interval (with/without
    // photons)
    //   - in particular, line spanning negative time
}

// NOLINTEND
