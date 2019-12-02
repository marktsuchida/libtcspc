#pragma once

#include "DecodedEvent.hpp"

#include <deque>
#include <stdexcept>
#include <string>


struct PixelPhotonEvent {
    uint16_t microtime;
    uint16_t route;
    uint32_t x;
    uint32_t y;
    uint32_t frame;
};


// Receiver of pixel-assigned photon events
class PixelPhotonProcessor {
public:
    virtual ~PixelPhotonProcessor() = default;

    virtual void HandleBeginFrame() = 0;
    virtual void HandleEndFrame() = 0;
    virtual void HandlePixelPhoton(PixelPhotonEvent const& event) = 0;
    virtual void HandleError(std::string const& message) = 0;
    virtual void HandleFinish() = 0;
};


// Assign pixels to photons using line clock only
class LineClockPixellator : public DecodedEventProcessor {
    uint32_t const pixelsPerLine;
    uint32_t const linesPerFrame;
    uint32_t const maxFrames;

    int32_t const lineDelay; // in macro-time units
    uint32_t const lineTime; // in macro-time units
    decltype(MarkerEvent::bits) const lineMarkerMask;

    uint64_t latestTimestamp; // Latest observed macro-time

    // Cumulative line numbers (no reset on new frame)
    uint64_t nextLine; // Incremented on line startTime
    uint64_t currentLine; // Incremented on line finish
    // "Line startTime" is reception of line marker, at which point the line startTime
    // and finish macro-times are determined. "Line finish" is when we
    // determine that all photons for the line have been emitted downstream.
    // If nextLine > currentLine, we are in currentLine.
    // If nextLine == currentLine, we are between (nextLine - 1) and nextLine.

    // Start time of current line (valid between startTime and finish of line).
    uint64_t lineStartTime;

    // Buffer received photons until we can assign to pixel
    std::deque<ValidPhotonEvent> pendingPhotons;

    // Buffer line marks until we are ready to process
    std::deque<uint64_t> pendingLines; // marker macro-times

    std::shared_ptr<PixelPhotonProcessor> downstream;

    struct Error {
        std::string message;
        explicit Error(std::string m) : message(m) {}
    };

private:
    void UpdateTimeRange(uint64_t macrotime) {
        latestTimestamp = macrotime;
    }

    void EnqueuePhoton(ValidPhotonEvent const& event) {
        if (!downstream) {
            return; // Avoid buffering post-error
        }
        pendingPhotons.emplace_back(event);
    }

    void EnqueueLineMarker(uint64_t macrotime) {
        if (!downstream) {
            return; // Avoid buffering post-error
        }
        pendingLines.emplace_back(macrotime);
    }

    uint64_t CheckLineStart(uint64_t lineMarkerTime) {
        uint64_t startTime;
        if (lineDelay >= 0) {
            startTime = lineMarkerTime + lineDelay;
        }
        else {
            uint64_t minusDelay = -lineDelay;
            if (lineMarkerTime < minusDelay) {
                throw Error("Pixel at negative time");
            }
            startTime = lineMarkerTime - minusDelay;
        }
        if (startTime < lineStartTime + lineTime) {
            throw Error("Pixels overlapping in time");
        }
        return startTime;
    }

    void StartLine(uint64_t lineMarkerTime) {
        lineStartTime = CheckLineStart(lineMarkerTime);
        ++nextLine;

        bool newFrame = currentLine % linesPerFrame == 0;
        if (newFrame) {
            // Check for last frame here in case maxFrames == 0.
            if (currentLine / linesPerFrame == maxFrames) {
                if (downstream) {
                    downstream->HandleFinish();
                    downstream.reset();
                }
            }

            if (downstream) {
                downstream->HandleBeginFrame();
            }
        }
    }

    void FinishLine() {
        ++currentLine;

        bool endFrame = currentLine % linesPerFrame == 0;
        if (endFrame) {
            if (downstream) {
                downstream->HandleEndFrame();
            }

            // Check for last frame here to send finish as soon as possible.
            // (The case of maxFrames == 0 is not handled here.)
            if (currentLine / linesPerFrame == maxFrames) {
                if (downstream) {
                    downstream->HandleFinish();
                    downstream.reset();
                }
            }
        }
    }

    void EmitPhoton(ValidPhotonEvent const& event) {
        PixelPhotonEvent newEvent;
        newEvent.frame = static_cast<uint32_t>(currentLine / linesPerFrame);
        newEvent.y = static_cast<uint32_t>(currentLine % linesPerFrame);
        auto timeInLine = event.macrotime - lineStartTime;
        newEvent.x = static_cast<uint32_t>(pixelsPerLine * timeInLine / lineTime);
        newEvent.route = event.route;
        newEvent.microtime = event.microtime;
        if (downstream) {
            downstream->HandlePixelPhoton(newEvent);
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
            uint64_t lineMarkerTime = pendingLines.front();
            pendingLines.pop_front();

            StartLine(lineMarkerTime);
        }
        // Else we are already in a line

        // Discard all photons before current line
        while (!pendingPhotons.empty()) {
            auto const& photon = pendingPhotons.front();
            if (photon.macrotime >= lineStartTime) {
                break;
            }
            pendingPhotons.pop_front();
        }

        // Emit all buffered photons for current line
        auto lineEndTime = lineStartTime + lineTime;
        while (!pendingPhotons.empty()) {
            auto const& photon = pendingPhotons.front();
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
        }
        else {
            return false; // Still in line but no more photons
        }
    }

    // When this function returns, all photons that can be emitted have been
    // emitted and all frames (and, internally, lines) for which we have seen
    // all photons have been finished.
    void ProcessPhotonsAndLines() {
        if (!downstream) {
            return;
        }
        try {
            while (ProcessLinePhotons())
                ;
        }
        catch (Error const& e) {
            if (downstream) {
                downstream->HandleError(e.message);
                downstream.reset();
            }
        }
    }

public:
    LineClockPixellator(uint32_t pixelsPerLine, uint32_t linesPerFrame,
        uint32_t maxFrames,
        int32_t lineDelay, uint32_t lineTime, uint32_t lineMarkerBit,
        std::shared_ptr<PixelPhotonProcessor> downstream) :
        pixelsPerLine(pixelsPerLine),
        linesPerFrame(linesPerFrame),
        maxFrames(maxFrames),
        lineDelay(lineDelay),
        lineTime(lineTime),
        lineMarkerMask(1 << lineMarkerBit),
        latestTimestamp(0),
        nextLine(0),
        currentLine(0),
        lineStartTime(0),
        downstream(downstream)
    {
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

    void HandleTimestamp(DecodedEvent const& event) override {
        auto prevTimestamp = latestTimestamp;
        UpdateTimeRange(event.macrotime);
        // We could call ProcessPhotonsAndLines() to emit all lines that are
        // complete, but deferring can significantly improve performance when a
        // timestamp is sent for every macro-time overflow.

        // Temporary: we need to process all buffered data based on time stamps
        // only, because currently we don't receive a "finish" event from
        // OpenScanLib when doing a finite-frame acquisition.
        // To avoid inefficiency, limit the rate (arbitrary for now).
        if (latestTimestamp > prevTimestamp + 800000) {
            ProcessPhotonsAndLines();
        }
    }

    void HandleDataLost(DataLostEvent const& event) override {
        UpdateTimeRange(event.macrotime);
        ProcessPhotonsAndLines();
        if (downstream) {
            downstream->HandleError("Data lost due to device buffer (FIFO) overflow");
            downstream.reset();
        }
    }

    void HandleValidPhoton(ValidPhotonEvent const& event) override {
        UpdateTimeRange(event.macrotime);
        EnqueuePhoton(event);
        // A small amount of buffering can improve performance (buffering
        // larger numbers is less effective)
        if (pendingPhotons.size() > 64) {
            ProcessPhotonsAndLines();
        }
    }

    void HandleInvalidPhoton(InvalidPhotonEvent const& event) override {
        UpdateTimeRange(event.macrotime);
        // We could call ProcessPhotonsAndLines() to emit all lines that are
        // complete, but deferring can improve performance.
    }

    void HandleMarker(MarkerEvent const& event) override {
        UpdateTimeRange(event.macrotime);
        if (event.bits & lineMarkerMask) {
            EnqueueLineMarker(event.macrotime);
            // We could call ProcessPhotonsAndLines() for all markers, but that
            // may degrade performance if a non-line marker (e.g. an unused
            // pixel marker) is frequent.
            ProcessPhotonsAndLines();
        }
    }

    void HandleError(std::string const& message) override {
        ProcessPhotonsAndLines(); // Emit any buffered data
        if (downstream) {
            downstream->HandleError(message);
            downstream.reset();
        }
    }

    void HandleFinish() override {
        ProcessPhotonsAndLines(); // Emit any buffered data

        // Note we do _not_ end the current frame: if it is incomplete,
        // downstream decides what to do with it.

        if (downstream) {
            downstream->HandleFinish();
            downstream.reset();
        }
    }

    // Emit all buffered data (for testing)
    void Flush() {
        ProcessPhotonsAndLines();
    }
};
