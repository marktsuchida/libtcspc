#pragma once

#include "PixelPhotonEvent.hpp"

#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>


template <
    typename T,
    typename U,
    typename = std::enable_if_t<sizeof(T) >= sizeof(U)>>
inline constexpr T SaturatingAdd(T a, U b) {
    T c = a + b;
    if (c < a)
        c = -1;
    return c;
}


template <
    typename T,
    typename = std::enable_if_t<std::is_unsigned<T>::value>>
class Histogram {
    uint32_t const timeBits;
    uint32_t const inputTimeBits;
    std::size_t const width;
    std::size_t const height;

    std::unique_ptr<T[]> hist;

public:
    ~Histogram() = default;
    Histogram(Histogram const& rhs) = delete;
    Histogram& operator=(Histogram const& rhs) = delete;
    Histogram(Histogram&& rhs) = default;
    Histogram& operator=(Histogram&& rhs) = default;

    // Warning: Newly constructed histogram is not zeroed (for efficiency)
    Histogram(uint32_t timeBits, uint32_t inputTimeBits, std::size_t width, std::size_t height) :
        timeBits(timeBits),
        inputTimeBits(inputTimeBits),
        width(width),
        height(height),
        hist(std::make_unique<T[]>(GetNumberOfElements()))
    {
        if (timeBits > inputTimeBits) {
            throw std::invalid_argument("Histogram time bits must not be greater than input bits");
        }
    }

    void Clear() noexcept {
        memset(hist.get(), 0, GetNumberOfElements() * sizeof(T));
    }

    uint32_t GetNumberOfTimeBins() const noexcept {
        return 1 << timeBits;
    }

    std::size_t GetNumberOfElements() const noexcept {
        return GetNumberOfTimeBins() * width * height;
    }

    void Increment(std::size_t t, std::size_t x, std::size_t y) noexcept {
        auto tReduced = t >> (inputTimeBits - timeBits);
        auto index = (y * width + x) * GetNumberOfTimeBins() + tReduced;
        hist[index] = SaturatingAdd(hist[index], T(1));
    }

    T const* Get() const noexcept {
        return hist.get();
    }

    // TODO Might be better to construct intensity image on the fly along with
    // the histogram (profile to determine)
    template <
        typename U,
        typename = std::enable_if_t<std::is_unsigned<U>::value && sizeof(U) >= sizeof(T)>>
    void AddToIntensityImage(U* image) {
        std::size_t n = width * height;
        auto nTimeBins = GetNumberOfTimeBins();
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t t = 0; t < nTimeBins; ++t) {
                image[i] = SaturatingAdd(image[i], hist[t + nTimeBins * i]);
            }
        }
    }

    Histogram& operator+=(Histogram<T> const& rhs) {
        if (rhs.timeBits != timeBits ||
            rhs.width != width ||
            rhs.height != height) {
            abort(); // Programming error
        }

        std::size_t n = GetNumberOfElements();
        for (std::size_t i = 0; i < n; ++i) {
            hist[i] = SaturatingAdd(hist[i], rhs.hist[i]);
        }

        return *this;
    }
};


// Receiver of frame-by-frame histogram events
template <typename T>
class HistogramProcessor {
public:
    virtual ~HistogramProcessor() = default;

    virtual void HandleError(std::string const& message) = 0;
    virtual void HandleFrame(Histogram<T> const& histogram) = 0;
    virtual void HandleFinish() = 0;
};


// Collect pixel-assiend photon events into a series of histograms
template <typename T>
class Histogrammer : public PixelPhotonProcessor {
    Histogram<T> histogram;

    std::shared_ptr<HistogramProcessor<T>> downstream;

public:
    Histogrammer(Histogram<T>&& histogram, std::shared_ptr<HistogramProcessor<T>> downstream) :
        histogram(std::move(histogram)),
        downstream(downstream)
    {}

    void HandleBeginFrame() override {
        histogram.Clear();
    }

    void HandleEndFrame() override {
        if (downstream) {
            downstream->HandleFrame(histogram);
        }
    }

    void HandlePixelPhoton(PixelPhotonEvent const& event) override {
        // TODO We are ignoring route for now
        histogram.Increment(event.microtime, event.x, event.y);
    }

    void HandleError(std::string const& message) override {
        if (downstream) {
            downstream->HandleError(message);
            downstream.reset();
        }
    }

    void HandleFinish() override {
        if (downstream) {
            downstream->HandleFinish();
            downstream.reset();
        }
    }
};


// Accumulate a series of histograms
template <typename T>
class HistogramAccumulator : public HistogramProcessor<T> {
    Histogram<T> cumulative;

    std::shared_ptr<HistogramProcessor<T>> downstream;

public:
    HistogramAccumulator(Histogram<T>&& histogram, std::shared_ptr<HistogramProcessor<T>> downstream) :
        cumulative(std::move(histogram)),
        downstream(downstream)
    {}

    void HandleError(std::string const& message) override {
        if (downstream) {
            downstream->HandleError(message);
            downstream.reset();
        }
    }

    void HandleFrame(Histogram<T> const& histogram) override {
        cumulative += histogram;
        if (downstream) {
            downstream->HandleFrame(cumulative);
        }
    }

    void HandleFinish() override {
        if (downstream) {
            downstream->HandleFinish();
            downstream.reset();
        }
    }
};
