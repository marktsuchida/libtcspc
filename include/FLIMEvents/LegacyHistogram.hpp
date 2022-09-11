/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "EventSet.hpp"
#include "PixelPhotonEvents.hpp"

#include <algorithm>
#include <cassert>
#include <deque>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

//! \cond TO_BE_REMOVED

namespace flimevt {

namespace internal {

template <typename T, typename U,
          typename = std::enable_if_t<std::is_unsigned_v<T> &&
                                      std::is_unsigned_v<U> &&
                                      sizeof(T) >= sizeof(U)>>
inline constexpr T saturating_add(T a, U b) noexcept {
    T c = a + b;
    if (c < a)
        c = -1;
    return c;
}

} // namespace internal

template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
class legacy_histogram {
    std::uint32_t timeBits;
    std::uint32_t inputTimeBits;
    bool reverseTime;
    std::size_t width;
    std::size_t height;

    std::unique_ptr<T[]> hist;

  public:
    ~legacy_histogram() = default;
    legacy_histogram(legacy_histogram const &rhs) = delete;
    legacy_histogram &operator=(legacy_histogram const &rhs) = delete;
    legacy_histogram(legacy_histogram &&rhs) = default;
    legacy_histogram &operator=(legacy_histogram &&rhs) = default;

    // Default constructor creates a "moved out" object.
    legacy_histogram() = default;

    // Warning: Newly constructed histogram is not zeroed (for efficiency)
    explicit legacy_histogram(std::uint32_t timeBits,
                              std::uint32_t inputTimeBits, bool reverseTime,
                              std::size_t width, std::size_t height)
        : timeBits(timeBits), inputTimeBits(inputTimeBits),
          reverseTime(reverseTime), width(width), height(height),
          hist(std::make_unique<T[]>(get_number_of_elements())) {
        if (timeBits > inputTimeBits) {
            throw std::invalid_argument(
                "Histogram time bits must not be greater than input bits");
        }
    }

    bool is_valid() const noexcept { return hist.get(); }

    void clear() noexcept {
        std::fill_n(hist.get(), get_number_of_elements(), T(0));
    }

    std::uint32_t get_time_bits() const noexcept { return timeBits; }

    std::uint32_t get_input_time_bits() const noexcept {
        return inputTimeBits;
    }

    bool get_reverse_time() const noexcept { return reverseTime; }

    std::uint32_t get_number_of_time_bins() const noexcept {
        return 1 << timeBits;
    }

    std::size_t get_width() const noexcept { return width; }

    std::size_t get_height() const noexcept { return height; }

    std::size_t get_number_of_elements() const noexcept {
        return get_number_of_time_bins() * width * height;
    }

    void increment(std::size_t t, std::size_t x, std::size_t y) noexcept {
        auto tReduced = std::uint16_t(t >> (inputTimeBits - timeBits));
        auto tReversed =
            reverseTime ? (1 << timeBits) - 1 - tReduced : tReduced;
        auto index = (y * width + x) * get_number_of_time_bins() + tReversed;
        hist[index] = internal::saturating_add(hist[index], T(1));
    }

    T const *get() const noexcept { return hist.get(); }

    T *get() noexcept { return hist.get(); }

    legacy_histogram &operator+=(legacy_histogram<T> const &rhs) noexcept {
        assert(rhs.timeBits == timeBits && rhs.width == width &&
               rhs.height == height);

        // Note: using std::execution::par_unseq for this transform did not
        // improve run time of a typical decode-and-histogram workflow, but did
        // double CPU time. (GCC 9 + libtbb, Ubuntu 20.04)
        std::transform(hist.get(), hist.get() + get_number_of_elements(),
                       rhs.hist.get(), hist.get(), [](T const &a, T const &b) {
                           return internal::saturating_add(a, b);
                       });

        return *this;
    }
};

// Events for emitting histograms are non-copyable and non-movable to prevent
// buffering, as they contain the histogram by reference and are therefore only
// valid for the duration of the handle_event() call.

template <typename T> struct frame_histogram_event {
    legacy_histogram<T> const &histogram;

    frame_histogram_event(frame_histogram_event const &) = delete;
    frame_histogram_event &operator=(frame_histogram_event const &) = delete;
    frame_histogram_event(frame_histogram_event &&) = delete;
    frame_histogram_event &operator=(frame_histogram_event &&) = delete;
};

template <typename T> struct incomplete_frame_histogram_event {
    legacy_histogram<T> const &histogram;

    incomplete_frame_histogram_event(
        incomplete_frame_histogram_event const &) = delete;

    incomplete_frame_histogram_event &
    operator=(incomplete_frame_histogram_event const &) = delete;

    incomplete_frame_histogram_event(incomplete_frame_histogram_event &&) =
        delete;

    incomplete_frame_histogram_event &
    operator=(incomplete_frame_histogram_event &&) = delete;
};

template <typename T> struct final_cumulative_histogram_event {
    legacy_histogram<T> const &histogram;

    final_cumulative_histogram_event(
        final_cumulative_histogram_event const &) = delete;

    final_cumulative_histogram_event &
    operator=(final_cumulative_histogram_event const &) = delete;

    final_cumulative_histogram_event(final_cumulative_histogram_event &&) =
        delete;

    final_cumulative_histogram_event &
    operator=(final_cumulative_histogram_event &&) = delete;
};

template <typename T>
using frame_histogram_events =
    event_set<frame_histogram_event<T>, incomplete_frame_histogram_event<T>>;

template <typename T>
using cumulative_histogram_events =
    event_set<frame_histogram_event<T>, final_cumulative_histogram_event<T>>;

// Collect pixel-assiend photon events into a series of histograms
template <typename T, typename D> class histogrammer {
    legacy_histogram<T> histogram;
    bool frameInProgress;

    D downstream;

  public:
    explicit histogrammer(legacy_histogram<T> &&histogram, D &&downstream)
        : histogram(std::move(histogram)), frameInProgress(false),
          downstream(std::move(downstream)) {}

    // Rule of zero

    void handle_event(begin_frame_event const &) noexcept {
        histogram.clear();
        frameInProgress = true;
    }

    void handle_event(end_frame_event const &) noexcept {
        frameInProgress = false;
        downstream.handle_event(frame_histogram_event<T>{histogram});
    }

    void handle_event(pixel_photon_event const &event) noexcept {
        histogram.increment(event.difftime, event.x, event.y);
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (frameInProgress) {
            downstream.handle_event(
                incomplete_frame_histogram_event<T>{histogram});
        }
        downstream.handle_end(error);
    }
};

// Same as histogrammer, but requires incoming pixel photon events to be in
// sequential pixel order. Accesses frame histogram memory sequentially,
// although the performance gain from this may not be significant.
template <typename T, typename D> class sequential_histogrammer {
    legacy_histogram<T> histogram;

    std::size_t binsPerPixel;
    legacy_histogram<T> pixelHist;

    std::size_t pixelNo; // Within frame

    D downstream;

  public:
    explicit sequential_histogrammer(legacy_histogram<T> &&histogram,
                                     D &&downstream)
        : histogram(std::move(histogram)),
          binsPerPixel(this->histogram.get_number_of_time_bins()),
          pixelHist(this->histogram.get_time_bits(),
                    this->histogram.get_input_time_bits(),
                    this->histogram.get_reverse_time(), 1, 1),
          pixelNo(this->histogram.get_width() * this->histogram.get_height()),
          downstream(std::move(downstream)) {}

    void handle_event(begin_frame_event const &) noexcept {
        pixelNo = 0;
        pixelHist.clear();
    }

    void handle_event(end_frame_event const &) noexcept {
        SkipToPixelNo(histogram.get_width() * histogram.get_height());
        downstream.handle_event(frame_histogram_event<T>{histogram});
    }

    void handle_event(pixel_photon_event const &event) noexcept {
        SkipToPixelNo(event.x + histogram.get_width() * event.y);
        pixelHist.increment(event.difftime, 0, 0);
    }

    void handle_end(std::exception_ptr error) noexcept {
        std::size_t const nPixels =
            histogram.get_width() * histogram.get_height();
        if (pixelNo < nPixels) {
            // Clear unfilled portion of incomplete frame
            std::fill_n(&histogram.get()[pixelNo * binsPerPixel],
                        (nPixels - pixelNo) * binsPerPixel, T(0));

            downstream.handle_event(
                incomplete_frame_histogram_event<T>{histogram});
        }
        downstream.handle_end(error);
    }

  private:
    void SkipToPixelNo(std::size_t newPixelNo) noexcept {
        assert(pixelNo <= newPixelNo);
        if (pixelNo < newPixelNo) {
            std::copy_n(pixelHist.get(), binsPerPixel,
                        &histogram.get()[pixelNo * binsPerPixel]);
            ++pixelNo;
            pixelHist.clear();
        }

        std::size_t const nSkippedPixels = newPixelNo - pixelNo;
        std::fill_n(&histogram.get()[pixelNo * binsPerPixel],
                    binsPerPixel * nSkippedPixels, T(0));
        pixelNo += nSkippedPixels;
        assert(pixelNo == newPixelNo);
    }
};

// Accumulate a series of histograms
// Guarantees complete frame upon finish (all zeros if there was no frame).
template <typename T, typename D> class histogram_accumulator {
    legacy_histogram<T> cumulative;

    D downstream;

  public:
    explicit histogram_accumulator(legacy_histogram<T> &&histogram,
                                   D &&downstream)
        : cumulative(std::move(histogram)), downstream(std::move(downstream)) {
    }

    void handle_event(frame_histogram_event<T> const &event) noexcept {
        cumulative += event.histogram;
        downstream.handle_event(frame_histogram_event<T>{cumulative});
    }

    void handle_event(incomplete_frame_histogram_event<T> const &) noexcept {
        // Ignore incomplete frames
    }

    void handle_end(std::exception_ptr error) noexcept {
        if (!error) {
            downstream.handle_event(
                final_cumulative_histogram_event<T>{cumulative});
        }
        downstream.handle_end(error);
    }
};

} // namespace flimevt

//! \endcond
