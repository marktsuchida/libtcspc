/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "pixel_photon_events.hpp"
#include "time_tagged_events.hpp"

#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>

//! \cond TO_BE_REMOVED
// NOLINTBEGIN

namespace tcspc {

// Assign pixels to photons using line clock only
template <typename D> class line_clock_pixellator {
    std::uint32_t const pixels_per_line;
    std::uint32_t const lines_per_frame;
    std::uint32_t const max_frames;

    std::int32_t const line_delay; // in macrotime units
    std::uint32_t const line_time; // in macrotime units
    std::int32_t const line_marker_channel;

    macrotime latest_timestamp; // Latest observed macrotime

    // Cumulative line numbers (no reset on new frame)
    std::uint64_t next_line;    // Incremented on line start_time
    std::uint64_t current_line; // Incremented on line finish
    // "Line start_time" is reception of line marker, at which point the line
    // start_time and finish macrotimes are determined. "Line finish" is when
    // we determine that all photons for the line have been emitted downstream.
    // If next_line > current_line, we are in current_line.
    // If next_line == current_line, we are between (next_line - 1) and
    // next_line.

    // Start time of current line, or -1 if no line started.
    macrotime line_start_time;

    // Buffer received photons until we can assign to pixel
    std::deque<time_correlated_count_event> pending_photons;

    // Buffer line marks until we are ready to process
    std::deque<macrotime> pending_lines; // marker macrotimes

    D downstream;
    bool stream_ended = false;

  private:
    void update_time_range(macrotime macrotime) noexcept {
        latest_timestamp = macrotime;
    }

    void enqueue_photon(time_correlated_count_event const &event) {
        if (stream_ended) {
            return; // Avoid buffering post-error
        }
        pending_photons.emplace_back(event);
    }

    void enqueue_line_marker(macrotime macrotime) {
        if (stream_ended) {
            return; // Avoid buffering post-error
        }
        pending_lines.emplace_back(macrotime);
    }

    macrotime check_line_start(macrotime line_marker_time) {
        macrotime start_time;
        if (line_delay >= 0) {
            start_time = line_marker_time + line_delay;
        } else {
            macrotime minus_delay = -line_delay;
            if (line_marker_time < minus_delay) {
                throw std::runtime_error("Pixel at negative time");
            }
            start_time = line_marker_time - minus_delay;
        }
        if (start_time < line_start_time + line_time &&
            line_start_time !=
                std::numeric_limits<decltype(line_start_time)>::max()) {
            throw std::runtime_error("Pixels overlapping in time");
        }
        return start_time;
    }

    void start_line(macrotime line_marker_time) {
        line_start_time = check_line_start(line_marker_time);
        ++next_line;

        bool new_frame = current_line % lines_per_frame == 0;
        if (new_frame) {
            // Check for last frame here in case max_frames == 0.
            if (current_line / lines_per_frame == max_frames) {
                if (!stream_ended) {
                    downstream.handle_end({});
                    stream_ended = true;
                }
            }

            if (!stream_ended) {
                downstream.handle_event(begin_frame_event());
            }
        }
    }

    void finish_line() {
        ++current_line;

        bool end_frame = current_line % lines_per_frame == 0;
        if (end_frame) {
            if (!stream_ended) {
                downstream.handle_event(end_frame_event());
            }

            // Check for last frame here to send finish as soon as possible.
            // (The case of max_frames == 0 is not handled here.)
            if (current_line / lines_per_frame == max_frames) {
                if (!stream_ended) {
                    downstream.handle_end({});
                    stream_ended = true;
                }
            }
        }
    }

    void emit_photon(time_correlated_count_event const &event) {
        pixel_photon_event new_event;
        new_event.frame =
            static_cast<std::uint32_t>(current_line / lines_per_frame);
        new_event.y =
            static_cast<std::uint32_t>(current_line % lines_per_frame);
        auto time_in_line = event.macrotime - line_start_time;
        new_event.x = static_cast<std::uint32_t>(pixels_per_line *
                                                 time_in_line / line_time);
        new_event.channel = event.channel;
        new_event.difftime = event.difftime;
        if (!stream_ended) {
            downstream.handle_event(new_event);
        }
    }

    // If in line, process photons in current line.
    // If between lines, start_time line if possible and do same.
    // Finish line if possible.
    // Return false if nothing more to process.
    bool process_line_photons() {
        if (next_line == current_line) { // Between lines
            if (pending_lines.empty()) {
                // Nothing to do until a new line can be started
                return false;
            }
            macrotime line_marker_time = pending_lines.front();
            pending_lines.pop_front();

            start_line(line_marker_time);
        }
        // Else we are already in a line

        // Discard all photons before current line
        while (!pending_photons.empty()) {
            auto const &photon = pending_photons.front();
            if (photon.macrotime >= line_start_time) {
                break;
            }
            pending_photons.pop_front();
        }

        // Emit all buffered photons for current line
        auto line_end_time = line_start_time + line_time;
        while (!pending_photons.empty()) {
            auto const &photon = pending_photons.front();
            if (photon.macrotime >= line_end_time) {
                break;
            }
            emit_photon(photon);
            pending_photons.pop_front();
        }

        // Finish line if we have seen all photons within it
        if (latest_timestamp >= line_end_time) {
            finish_line();
            return true; // There may be more lines to process
        } else {
            return false; // Still in line but no more photons
        }
    }

    // When this function returns, all photons that can be emitted have been
    // emitted and all frames (and, internally, lines) for which we have seen
    // all photons have been finished.
    void process_photons_and_lines() noexcept {
        if (stream_ended)
            return;

        try {
            while (process_line_photons())
                ;
        } catch (std::exception const &) {
            if (!stream_ended) {
                downstream.handle_end(std::current_exception());
                stream_ended = true;
            }
        }
    }

  public:
    explicit line_clock_pixellator(std::uint32_t pixels_per_line,
                                   std::uint32_t lines_per_frame,
                                   std::uint32_t max_frames,
                                   std::int32_t line_delay,
                                   std::uint32_t line_time,
                                   std::int32_t line_marker_channel,
                                   D &&downstream)
        : pixels_per_line(pixels_per_line), lines_per_frame(lines_per_frame),
          max_frames(max_frames), line_delay(line_delay), line_time(line_time),
          line_marker_channel(line_marker_channel), latest_timestamp(0),
          next_line(0), current_line(0), line_start_time(-1),
          downstream(std::move(downstream)) {
        if (pixels_per_line < 1) {
            throw std::invalid_argument("pixels_per_line must be positive");
        }
        if (lines_per_frame < 1) {
            throw std::invalid_argument("lines_per_frame must be positive");
        }
        if (line_time < 1) {
            throw std::invalid_argument("line_time must be positive");
        }
    }

    void handle_event(time_reached_event const &event) noexcept {
        auto prev_timestamp = latest_timestamp;
        update_time_range(event.macrotime);
        // We could call process_photons_and_lines() to emit all lines that are
        // complete, but deferring can significantly improve performance when a
        // timestamp is sent for every macrotime overflow.

        // Temporary: we need to process all buffered data based on time stamps
        // only, because currently we don't receive a "finish" event from
        // OpenScanLib when doing a finite-frame acquisition.
        // To avoid inefficiency, limit the rate (arbitrary for now).
        if (latest_timestamp > prev_timestamp + 800000) {
            process_photons_and_lines();
        }
    }

    void handle_event(data_lost_event const &event) noexcept {
        update_time_range(event.macrotime);
        process_photons_and_lines();
        if (!stream_ended) {
            downstream.handle_end(std::make_exception_ptr(std::runtime_error(
                "Data lost due to device buffer (FIFO) overflow")));
            stream_ended = true;
        }
    }

    void handle_event(time_correlated_count_event const &event) noexcept {
        update_time_range(event.macrotime);
        try {
            enqueue_photon(event);
        } catch (std::exception const &) {
            downstream.handle_end(std::current_exception());
            stream_ended = true;
        }
        // A small amount of buffering can improve performance (buffering
        // larger numbers is less effective)
        if (pending_photons.size() > 64) {
            process_photons_and_lines();
        }
    }

    void handle_event(marker_event const &event) noexcept {
        update_time_range(event.macrotime);
        if (event.channel == line_marker_channel) {
            try {
                enqueue_line_marker(event.macrotime);
            } catch (std::exception const &) {
                downstream.handle_end(std::current_exception());
                stream_ended = true;
            }
            // We could call process_photons_and_lines() for all markers, but
            // that may degrade performance if a non-line marker (e.g. an
            // unused pixel marker) is frequent.
            process_photons_and_lines();
        }
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        process_photons_and_lines(); // Emit any buffered data
        if (!stream_ended) {
            downstream.handle_end(error);
            stream_ended = true;
        }
    }

    // Emit all buffered data (for testing)
    void flush() { process_photons_and_lines(); }
};

} // namespace tcspc

// NOLINTEND
//! \endcond
