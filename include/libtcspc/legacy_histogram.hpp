/*
 * This file is part of libtcspc
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "pixel_photon_events.hpp"

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
// NOLINTBEGIN

namespace tcspc {

namespace internal {

template <typename T, typename U,
          typename = std::enable_if_t<std::is_unsigned_v<T> &&
                                      std::is_unsigned_v<U> &&
                                      sizeof(T) >= sizeof(U)>>
inline constexpr T saturating_add(T a, U b) noexcept {
    T c = a + b;
    if (c < a)
        c = T(-1);
    return c;
}

} // namespace internal

template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
class legacy_histogram {
    std::uint32_t time_bits;
    std::uint32_t input_time_bits;
    bool reverse_time;
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
    explicit legacy_histogram(std::uint32_t time_bits,
                              std::uint32_t input_time_bits, bool reverse_time,
                              std::size_t width, std::size_t height)
        : time_bits(time_bits), input_time_bits(input_time_bits),
          reverse_time(reverse_time), width(width), height(height),
          hist(std::make_unique<T[]>(get_number_of_elements())) {
        if (time_bits > input_time_bits) {
            throw std::invalid_argument(
                "Histogram time bits must not be greater than input bits");
        }
    }

    bool is_valid() const noexcept { return hist.get(); }

    void clear() noexcept {
        std::fill_n(hist.get(), get_number_of_elements(), T(0));
    }

    std::uint32_t get_time_bits() const noexcept { return time_bits; }

    std::uint32_t get_input_time_bits() const noexcept {
        return input_time_bits;
    }

    bool get_reverse_time() const noexcept { return reverse_time; }

    std::uint32_t get_number_of_time_bins() const noexcept {
        return 1 << time_bits;
    }

    std::size_t get_width() const noexcept { return width; }

    std::size_t get_height() const noexcept { return height; }

    std::size_t get_number_of_elements() const noexcept {
        return get_number_of_time_bins() * width * height;
    }

    void increment(std::size_t t, std::size_t x, std::size_t y) noexcept {
        auto t_reduced = std::uint16_t(t >> (input_time_bits - time_bits));
        auto t_reversed =
            reverse_time ? (1 << time_bits) - 1 - t_reduced : t_reduced;
        auto index = (y * width + x) * get_number_of_time_bins() +
                     internal::as_unsigned(t_reversed);
        hist[index] = internal::saturating_add(hist[index], T(1));
    }

    T const *get() const noexcept { return hist.get(); }

    T *get() noexcept { return hist.get(); }

    legacy_histogram &operator+=(legacy_histogram<T> const &rhs) noexcept {
        assert(rhs.time_bits == time_bits && rhs.width == width &&
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
    bool frame_in_progress;

    D downstream;

  public:
    explicit histogrammer(legacy_histogram<T> &&histogram, D &&downstream)
        : histogram(std::move(histogram)), frame_in_progress(false),
          downstream(std::move(downstream)) {}

    // Rule of zero

    void handle_event(begin_frame_event const &) noexcept {
        histogram.clear();
        frame_in_progress = true;
    }

    void handle_event(end_frame_event const &) noexcept {
        frame_in_progress = false;
        downstream.handle_event(frame_histogram_event<T>{histogram});
    }

    void handle_event(pixel_photon_event const &event) noexcept {
        histogram.increment(event.difftime, event.x, event.y);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        if (frame_in_progress) {
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

    std::size_t bins_per_pixel;
    legacy_histogram<T> pixel_hist;

    std::size_t pixel_no; // Within frame

    D downstream;

  public:
    explicit sequential_histogrammer(legacy_histogram<T> &&histogram,
                                     D &&downstream)
        : histogram(std::move(histogram)),
          bins_per_pixel(this->histogram.get_number_of_time_bins()),
          pixel_hist(this->histogram.get_time_bits(),
                     this->histogram.get_input_time_bits(),
                     this->histogram.get_reverse_time(), 1, 1),
          pixel_no(this->histogram.get_width() * this->histogram.get_height()),
          downstream(std::move(downstream)) {}

    void handle_event(begin_frame_event const &) noexcept {
        pixel_no = 0;
        pixel_hist.clear();
    }

    void handle_event(end_frame_event const &) noexcept {
        skip_to_pixel_no(histogram.get_width() * histogram.get_height());
        downstream.handle_event(frame_histogram_event<T>{histogram});
    }

    void handle_event(pixel_photon_event const &event) noexcept {
        skip_to_pixel_no(event.x + histogram.get_width() * event.y);
        pixel_hist.increment(event.difftime, 0, 0);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        std::size_t const n_pixels =
            histogram.get_width() * histogram.get_height();
        if (pixel_no < n_pixels) {
            // Clear unfilled portion of incomplete frame
            std::fill_n(&histogram.get()[pixel_no * bins_per_pixel],
                        (n_pixels - pixel_no) * bins_per_pixel, T(0));

            downstream.handle_event(
                incomplete_frame_histogram_event<T>{histogram});
        }
        downstream.handle_end(error);
    }

  private:
    void skip_to_pixel_no(std::size_t new_pixel_no) noexcept {
        assert(pixel_no <= new_pixel_no);
        if (pixel_no < new_pixel_no) {
            std::copy_n(pixel_hist.get(), bins_per_pixel,
                        &histogram.get()[pixel_no * bins_per_pixel]);
            ++pixel_no;
            pixel_hist.clear();
        }

        std::size_t const n_skipped_pixels = new_pixel_no - pixel_no;
        std::fill_n(&histogram.get()[pixel_no * bins_per_pixel],
                    bins_per_pixel * n_skipped_pixels, T(0));
        pixel_no += n_skipped_pixels;
        assert(pixel_no == new_pixel_no);
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

    void handle_end(std::exception_ptr const &error) noexcept {
        if (!error) {
            downstream.handle_event(
                final_cumulative_histogram_event<T>{cumulative});
        }
        downstream.handle_end(error);
    }
};

} // namespace tcspc

// NOLINTEND
//! \endcond
