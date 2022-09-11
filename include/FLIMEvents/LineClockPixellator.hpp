/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "PixelPhotonEvents.hpp"
#include "TimeTaggedEvents.hpp"

#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>

//! \cond TO_BE_REMOVED

namespace flimevt {

// Assign pixels to photons using line clock only
template <typename D> class line_clock_pixellator {
    std::uint32_t const pixelsPerLine;
    std::uint32_t const linesPerFrame;
    std::uint32_t const maxFrames;

    std::int32_t const lineDelay; // in macrotime units
    std::uint32_t const lineTime; // in macrotime units
    std::int32_t const lineMarkerChannel;

    macrotime latestTimestamp; // Latest observed macrotime

    // Cumulative line numbers (no reset on new frame)
    std::uint64_t nextLine;    // Incremented on line startTime
    std::uint64_t currentLine; // Incremented on line finish
    // "Line startTime" is reception of line marker, at which point the line
    // startTime and finish macrotimes are determined. "Line finish" is when
    // we determine that all photons for the line have been emitted downstream.
    // If nextLine > currentLine, we are in currentLine.
    // If nextLine == currentLine, we are between (nextLine - 1) and nextLine.

    // Start time of current line, or -1 if no line started.
    macrotime lineStartTime;

    // Buffer received photons until we can assign to pixel
    std::deque<time_correlated_count_event> pendingPhotons;

    // Buffer line marks until we are ready to process
    std::deque<macrotime> pendingLines; // marker macrotimes

    D downstream;
    bool streamEnded = false;

  private:
    void UpdateTimeRange(macrotime macrotime) noexcept {
        latestTimestamp = macrotime;
    }

    void EnqueuePhoton(time_correlated_count_event const &event) {
        if (streamEnded) {
            return; // Avoid buffering post-error
        }
        pendingPhotons.emplace_back(event);
    }

    void EnqueueLineMarker(macrotime macrotime) {
        if (streamEnded) {
            return; // Avoid buffering post-error
        }
        pendingLines.emplace_back(macrotime);
    }

    macrotime CheckLineStart(macrotime lineMarkerTime) {
        macrotime startTime;
        if (lineDelay >= 0) {
            startTime = lineMarkerTime + lineDelay;
        } else {
            macrotime minusDelay = -lineDelay;
            if (lineMarkerTime < minusDelay) {
                throw std::runtime_error("Pixel at negative time");
            }
            startTime = lineMarkerTime - minusDelay;
        }
        if (startTime < lineStartTime + lineTime &&
            lineStartTime !=
                std::numeric_limits<decltype(lineStartTime)>::max()) {
            throw std::runtime_error("Pixels overlapping in time");
        }
        return startTime;
    }

    void StartLine(macrotime lineMarkerTime) {
        lineStartTime = CheckLineStart(lineMarkerTime);
        ++nextLine;

        bool newFrame = currentLine % linesPerFrame == 0;
        if (newFrame) {
            // Check for last frame here in case maxFrames == 0.
            if (currentLine / linesPerFrame == maxFrames) {
                if (!streamEnded) {
                    downstream.handle_end({});
                    streamEnded = true;
                }
            }

            if (!streamEnded) {
                downstream.handle_event(begin_frame_event());
            }
        }
    }

    void FinishLine() {
        ++currentLine;

        bool endFrame = currentLine % linesPerFrame == 0;
        if (endFrame) {
            if (!streamEnded) {
                downstream.handle_event(end_frame_event());
            }

            // Check for last frame here to send finish as soon as possible.
            // (The case of maxFrames == 0 is not handled here.)
            if (currentLine / linesPerFrame == maxFrames) {
                if (!streamEnded) {
                    downstream.handle_end({});
                    streamEnded = true;
                }
            }
        }
    }

    void EmitPhoton(time_correlated_count_event const &event) {
        pixel_photon_event newEvent;
        newEvent.frame =
            static_cast<std::uint32_t>(currentLine / linesPerFrame);
        newEvent.y = static_cast<std::uint32_t>(currentLine % linesPerFrame);
        auto timeInLine = event.macrotime - lineStartTime;
        newEvent.x =
            static_cast<std::uint32_t>(pixelsPerLine * timeInLine / lineTime);
        newEvent.channel = event.channel;
        newEvent.difftime = event.difftime;
        if (!streamEnded) {
            downstream.handle_event(newEvent);
        }
    }

    // If in line, process photons in current line.
    // If between lines, startTime line if possible and do same.
    // Finish line if possible.
    // Return false if nothing more to process.
    bool ProcessLinePhotons() {
        if (nextLine == currentLine) { // Between lines
            if (pendingLines.empty()) {
                // Nothing to do until a new line can be started
                return false;
            }
            macrotime lineMarkerTime = pendingLines.front();
            pendingLines.pop_front();

            StartLine(lineMarkerTime);
        }
        // Else we are already in a line

        // Discard all photons before current line
        while (!pendingPhotons.empty()) {
            auto const &photon = pendingPhotons.front();
            if (photon.macrotime >= lineStartTime) {
                break;
            }
            pendingPhotons.pop_front();
        }

        // Emit all buffered photons for current line
        auto lineEndTime = lineStartTime + lineTime;
        while (!pendingPhotons.empty()) {
            auto const &photon = pendingPhotons.front();
            if (photon.macrotime >= lineEndTime) {
                break;
            }
            EmitPhoton(photon);
            pendingPhotons.pop_front();
        }

        // Finish line if we have seen all photons within it
        if (latestTimestamp >= lineEndTime) {
            FinishLine();
            return true; // There may be more lines to process
        } else {
            return false; // Still in line but no more photons
        }
    }

    // When this function returns, all photons that can be emitted have been
    // emitted and all frames (and, internally, lines) for which we have seen
    // all photons have been finished.
    void ProcessPhotonsAndLines() noexcept {
        if (streamEnded)
            return;

        try {
            while (ProcessLinePhotons())
                ;
        } catch (std::exception const &) {
            if (!streamEnded) {
                downstream.handle_end(std::current_exception());
                streamEnded = true;
            }
        }
    }

  public:
    explicit line_clock_pixellator(
        std::uint32_t pixelsPerLine, std::uint32_t linesPerFrame,
        std::uint32_t maxFrames, std::int32_t lineDelay,
        std::uint32_t lineTime, std::int32_t lineMarkerChannel, D &&downstream)
        : pixelsPerLine(pixelsPerLine), linesPerFrame(linesPerFrame),
          maxFrames(maxFrames), lineDelay(lineDelay), lineTime(lineTime),
          lineMarkerChannel(lineMarkerChannel), latestTimestamp(0),
          nextLine(0), currentLine(0), lineStartTime(-1),
          downstream(std::move(downstream)) {
        if (pixelsPerLine < 1) {
            throw std::invalid_argument("pixelsPerLine must be positive");
        }
        if (linesPerFrame < 1) {
            throw std::invalid_argument("linesPerFrame must be positive");
        }
        if (lineTime < 1) {
            throw std::invalid_argument("lineTime must be positive");
        }
    }

    void handle_event(time_reached_event const &event) noexcept {
        auto prevTimestamp = latestTimestamp;
        UpdateTimeRange(event.macrotime);
        // We could call ProcessPhotonsAndLines() to emit all lines that are
        // complete, but deferring can significantly improve performance when a
        // timestamp is sent for every macrotime overflow.

        // Temporary: we need to process all buffered data based on time stamps
        // only, because currently we don't receive a "finish" event from
        // OpenScanLib when doing a finite-frame acquisition.
        // To avoid inefficiency, limit the rate (arbitrary for now).
        if (latestTimestamp > prevTimestamp + 800000) {
            ProcessPhotonsAndLines();
        }
    }

    void handle_event(data_lost_event const &event) noexcept {
        UpdateTimeRange(event.macrotime);
        ProcessPhotonsAndLines();
        if (!streamEnded) {
            downstream.handle_end(std::make_exception_ptr(std::runtime_error(
                "Data lost due to device buffer (FIFO) overflow")));
            streamEnded = true;
        }
    }

    void handle_event(time_correlated_count_event const &event) noexcept {
        UpdateTimeRange(event.macrotime);
        try {
            EnqueuePhoton(event);
        } catch (std::exception const &) {
            downstream.handle_end(std::current_exception());
            streamEnded = true;
        }
        // A small amount of buffering can improve performance (buffering
        // larger numbers is less effective)
        if (pendingPhotons.size() > 64) {
            ProcessPhotonsAndLines();
        }
    }

    void handle_event(marker_event const &event) noexcept {
        UpdateTimeRange(event.macrotime);
        if (event.channel == lineMarkerChannel) {
            try {
                EnqueueLineMarker(event.macrotime);
            } catch (std::exception const &) {
                downstream.handle_end(std::current_exception());
                streamEnded = true;
            }
            // We could call ProcessPhotonsAndLines() for all markers, but that
            // may degrade performance if a non-line marker (e.g. an unused
            // pixel marker) is frequent.
            ProcessPhotonsAndLines();
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
        ProcessPhotonsAndLines(); // Emit any buffered data
        if (!streamEnded) {
            downstream.handle_end(error);
            streamEnded = true;
        }
    }

    // Emit all buffered data (for testing)
    void flush() { ProcessPhotonsAndLines(); }
};

} // namespace flimevt

//! \endcond
