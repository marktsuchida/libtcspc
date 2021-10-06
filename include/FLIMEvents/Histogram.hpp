#pragma once

#include "EventSet.hpp"
#include "PixelPhotonEvent.hpp"

#include <algorithm>
#include <cassert>
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
          typename = std::enable_if_t<std::is_unsigned_v<T> &&
                                      std::is_unsigned_v<U> &&
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
        std::fill_n(hist.get(), GetNumberOfElements(), T(0));
    }

    uint32_t GetTimeBits() const noexcept { return timeBits; }

    uint32_t GetInputTimeBits() const noexcept { return inputTimeBits; }

    bool GetReverseTime() const noexcept { return reverseTime; }

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

    T *Get() noexcept { return hist.get(); }

    Histogram &operator+=(Histogram<T> const &rhs) noexcept {
        assert(rhs.timeBits == timeBits && rhs.width == width &&
               rhs.height == height);

        // Note: using std::execution::par_unseq for this transform did not
        // improve run time of a typical decode-and-histogram workflow, but did
        // double CPU time. (GCC 9 + libtbb, Ubuntu 20.04)
        std::transform(hist.get(), hist.get() + GetNumberOfElements(),
                       rhs.hist.get(), hist.get(), [](T const &a, T const &b) {
                           return internal::SaturatingAdd(a, b);
                       });

        return *this;
    }
};

// Events for emitting histograms are non-copyable and non-movable to prevent
// buffering, as they contain the histogram by reference and are therefore only
// valid for the duration of the HandleEvent() call.

template <typename T> struct FrameHistogramEvent {
    Histogram<T> const &histogram;

    FrameHistogramEvent(FrameHistogramEvent const &) = delete;
    FrameHistogramEvent &operator=(FrameHistogramEvent const &) = delete;
    FrameHistogramEvent(FrameHistogramEvent &&) = delete;
    FrameHistogramEvent &operator=(FrameHistogramEvent &&) = delete;
};

template <typename T> struct IncompleteFrameHistogramEvent {
    Histogram<T> const &histogram;

    IncompleteFrameHistogramEvent(IncompleteFrameHistogramEvent const &) =
        delete;

    IncompleteFrameHistogramEvent &
    operator=(IncompleteFrameHistogramEvent const &) = delete;

    IncompleteFrameHistogramEvent(IncompleteFrameHistogramEvent &&) = delete;

    IncompleteFrameHistogramEvent &
    operator=(IncompleteFrameHistogramEvent &&) = delete;
};

template <typename T> struct FinalCumulativeHistogramEvent {
    Histogram<T> const &histogram;

    FinalCumulativeHistogramEvent(FinalCumulativeHistogramEvent const &) =
        delete;

    FinalCumulativeHistogramEvent &
    operator=(FinalCumulativeHistogramEvent const &) = delete;

    FinalCumulativeHistogramEvent(FinalCumulativeHistogramEvent &&) = delete;

    FinalCumulativeHistogramEvent &
    operator=(FinalCumulativeHistogramEvent &&) = delete;
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
        if (frameInProgress) {
            downstream.HandleEvent(
                IncompleteFrameHistogramEvent<T>{histogram});
        }
        downstream.HandleEnd(error);
    }
};

// Same as Histogrammer, but requires incoming pixel photon events to be in
// sequential pixel order. Accesses frame histogram memory sequentially,
// although the performance gain from this may not be significant.
template <typename T, typename D> class SequentialHistogrammer {
    Histogram<T> histogram;

    std::size_t binsPerPixel;
    Histogram<T> pixelHist;

    std::size_t pixelNo; // Within frame

    D downstream;

  public:
    explicit SequentialHistogrammer(Histogram<T> &&histogram, D &&downstream)
        : histogram(std::move(histogram)),
          binsPerPixel(this->histogram.GetNumberOfTimeBins()),
          pixelHist(this->histogram.GetTimeBits(),
                    this->histogram.GetInputTimeBits(),
                    this->histogram.GetReverseTime(), 1, 1),
          pixelNo(this->histogram.GetWidth() * this->histogram.GetHeight()),
          downstream(std::move(downstream)) {}

    void HandleEvent(BeginFrameEvent const &) noexcept {
        pixelNo = 0;
        pixelHist.Clear();
    }

    void HandleEvent(EndFrameEvent const &) noexcept {
        SkipToPixelNo(histogram.GetWidth() * histogram.GetHeight());
        downstream.HandleEvent(FrameHistogramEvent<T>{histogram});
    }

    void HandleEvent(PixelPhotonEvent const &event) noexcept {
        SkipToPixelNo(event.x + histogram.GetWidth() * event.y);
        pixelHist.Increment(event.microtime, 0, 0);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        std::size_t const nPixels =
            histogram.GetWidth() * histogram.GetHeight();
        if (pixelNo < nPixels) {
            // Clear unfilled portion of incomplete frame
            std::fill_n(&histogram.Get()[pixelNo * binsPerPixel],
                        (nPixels - pixelNo) * binsPerPixel, T(0));

            downstream.HandleEvent(
                IncompleteFrameHistogramEvent<T>{histogram});
        }
        downstream.HandleEnd(error);
    }

  private:
    void SkipToPixelNo(std::size_t newPixelNo) noexcept {
        assert(pixelNo <= newPixelNo);
        if (pixelNo < newPixelNo) {
            std::copy_n(pixelHist.Get(), binsPerPixel,
                        &histogram.Get()[pixelNo * binsPerPixel]);
            ++pixelNo;
            pixelHist.Clear();
        }

        std::size_t const nSkippedPixels = newPixelNo - pixelNo;
        std::fill_n(&histogram.Get()[pixelNo * binsPerPixel],
                    binsPerPixel * nSkippedPixels, T(0));
        pixelNo += nSkippedPixels;
        assert(pixelNo == newPixelNo);
    }
};

// Accumulate a series of histograms
// Guarantees complete frame upon finish (all zeros if there was no frame).
template <typename T, typename D> class HistogramAccumulator {
    Histogram<T> cumulative;

    D downstream;

  public:
    explicit HistogramAccumulator(Histogram<T> &&histogram, D &&downstream)
        : cumulative(std::move(histogram)), downstream(std::move(downstream)) {
    }

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
