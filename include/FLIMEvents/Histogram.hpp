#pragma once

#include "DynamicPolymorphism.hpp"
#include "PixelPhotonEvent.hpp"

#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace flimevt {

namespace internal {

template <typename T, typename U,
          typename =
              std::enable_if_t<std::is_unsigned_v<T> && std::is_unsigned_v<U> &&
                               sizeof(T) >= sizeof(U)>>
inline constexpr T SaturatingAdd(T a, U b) noexcept {
    T c = a + b;
    if (c < a)
        c = -1;
    return c;
}

} // namespace internal

template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
class Histogram {
    uint32_t timeBits;
    uint32_t inputTimeBits;
    bool reverseTime;
    std::size_t width;
    std::size_t height;

    std::unique_ptr<T[]> hist;

  public:
    ~Histogram() = default;
    Histogram(Histogram const &rhs) = delete;
    Histogram &operator=(Histogram const &rhs) = delete;
    Histogram(Histogram &&rhs) = default;
    Histogram &operator=(Histogram &&rhs) = default;

    // Default constructor creates a "moved out" object.
    Histogram() = default;

    // Warning: Newly constructed histogram is not zeroed (for efficiency)
    explicit Histogram(uint32_t timeBits, uint32_t inputTimeBits,
                       bool reverseTime, std::size_t width, std::size_t height)
        : timeBits(timeBits), inputTimeBits(inputTimeBits),
          reverseTime(reverseTime), width(width), height(height),
          hist(std::make_unique<T[]>(GetNumberOfElements())) {
        if (timeBits > inputTimeBits) {
            throw std::invalid_argument(
                "Histogram time bits must not be greater than input bits");
        }
    }

    bool IsValid() const noexcept { return hist.get(); }

    void Clear() noexcept {
        memset(hist.get(), 0, GetNumberOfElements() * sizeof(T));
    }

    uint32_t GetTimeBits() const noexcept { return timeBits; }

    uint32_t GetNumberOfTimeBins() const noexcept { return 1 << timeBits; }

    std::size_t GetWidth() const noexcept { return width; }

    std::size_t GetHeight() const noexcept { return height; }

    std::size_t GetNumberOfElements() const noexcept {
        return GetNumberOfTimeBins() * width * height;
    }

    void Increment(std::size_t t, std::size_t x, std::size_t y) noexcept {
        auto tReduced = uint16_t(t >> (inputTimeBits - timeBits));
        auto tReversed =
            reverseTime ? (1 << timeBits) - 1 - tReduced : tReduced;
        auto index = (y * width + x) * GetNumberOfTimeBins() + tReversed;
        hist[index] = internal::SaturatingAdd(hist[index], T(1));
    }

    T const *Get() const noexcept { return hist.get(); }

    Histogram &operator+=(Histogram<T> const &rhs) noexcept {
        if (rhs.timeBits != timeBits || rhs.width != width ||
            rhs.height != height) {
            abort(); // Programming error
        }

        std::size_t n = GetNumberOfElements();
        for (std::size_t i = 0; i < n; ++i) {
            hist[i] = internal::SaturatingAdd(hist[i], rhs.hist[i]);
        }

        return *this;
    }
};

template <typename T> struct FrameHistogramEvent {
    Histogram<T> const &histogram;
};

template <typename T> struct IncompleteFrameHistogramEvent {
    Histogram<T> const &histogram;
};

template <typename T> struct FinalCumulativeHistogramEvent {
    Histogram<T> const &histogram;
};

template <typename T>
using FrameHistogramEvents =
    EventSet<FrameHistogramEvent<T>, IncompleteFrameHistogramEvent<T>>;

template <typename T>
using CumulativeHistogramEvents =
    EventSet<FrameHistogramEvent<T>, FinalCumulativeHistogramEvent<T>>;

// Collect pixel-assiend photon events into a series of histograms
template <typename T, typename D> class Histogrammer {
    Histogram<T> histogram;
    bool frameInProgress;

    D downstream;

  public:
    explicit Histogrammer(Histogram<T> &&histogram, D &&downstream)
        : histogram(std::move(histogram)), frameInProgress(false),
          downstream(std::move(downstream)) {}

    // Rule of zero

    void HandleEvent(BeginFrameEvent const &) noexcept {
        histogram.Clear();
        frameInProgress = true;
    }

    void HandleEvent(EndFrameEvent const &) noexcept {
        frameInProgress = false;
        downstream.HandleEvent(FrameHistogramEvent<T>{histogram});
    }

    void HandleEvent(PixelPhotonEvent const &event) noexcept {
        histogram.Increment(event.microtime, event.x, event.y);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

// Accumulate a series of histograms
// Guarantees complete frame upon finish (all zeros if there was no frame).
template <typename T, typename D> class HistogramAccumulator {
    Histogram<T> cumulative;

    D downstream;

  public:
    explicit HistogramAccumulator(Histogram<T> &&histogram, D &&downstream)
        : cumulative(std::move(histogram)), downstream(std::move(downstream)) {}

    void HandleEvent(FrameHistogramEvent<T> const &event) noexcept {
        cumulative += event.histogram;
        downstream.HandleEvent(FrameHistogramEvent<T>{cumulative});
    }

    void HandleEvent(IncompleteFrameHistogramEvent<T> const &) noexcept {
        // Ignore incomplete frames
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        if (!error) {
            downstream.HandleEvent(
                FinalCumulativeHistogramEvent<T>{cumulative});
        }
        downstream.HandleEnd(error);
    }
};

} // namespace flimevt
