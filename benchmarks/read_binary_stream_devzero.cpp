/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/read_binary_stream.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <utility>
#include <vector>

namespace tcspc {

// Compare ifstream to C FILE *, buffering on or off, different read sizes.
// The optimum may also depend on downstream processing, which is no-op here.

// These benchmarks are good enough to conclude that unbuffered C files perform
// best. Finding the optimal read size requires testing a larger range (for
// large amounts of data (1 GiB), much larger read sizes (megabytes) were
// faster on an Apple M1 Pro laptop). It is probably also affected by what is
// done downstream.

namespace {

// Access input stream via reference, to ensure stream creation is not
// dominant. (Effect was moderate for 1 MiB reads.)
template <typename InputStream> class ref_input_stream {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    InputStream &stream;

  public:
    explicit ref_input_stream(InputStream &stream) : stream(stream) {}

    auto is_error() noexcept -> bool { return stream.is_error(); }
    auto is_eof() noexcept -> bool { return stream.is_eof(); }
    auto is_good() noexcept -> bool { return stream.is_good(); }

    auto tell() noexcept -> std::optional<std::uint64_t> {
        return stream.tell();
    }

    auto skip(std::uint64_t bytes) noexcept -> bool {
        return stream.skip(bytes);
    }

    auto read(span<std::byte> buffer) noexcept -> std::uint64_t {
        return stream.read(buffer);
    }
};

class unoptimized_null_sink {
  public:
    template <typename Event> void handle_event(Event const &event) noexcept {
        benchmark::DoNotOptimize(event);
    }

    static void handle_end(std::exception_ptr const &error) noexcept {
        benchmark::DoNotOptimize(error);
    }
};

constexpr std::size_t total_bytes = 1 << 20;

} // namespace

void ifstream_unbuf(benchmark::State &state) {
    auto stream =
        internal::unbuffered_binary_ifstream_input_stream("/dev/zero");
    auto const read_size = static_cast<std::size_t>(state.range(0));
    auto pool = std::make_shared<object_pool<std::vector<int>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto src =
            read_binary_stream<int>(ref_input_stream(stream), total_bytes,
                                    pool, read_size, unoptimized_null_sink());
        src.pump_events();
    }
}

void ifstream(benchmark::State &state) {
    auto stream = internal::binary_ifstream_input_stream("/dev/zero");
    auto const read_size = static_cast<std::size_t>(state.range(0));
    auto pool = std::make_shared<object_pool<std::vector<int>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto src =
            read_binary_stream<int>(ref_input_stream(stream), total_bytes,
                                    pool, read_size, unoptimized_null_sink());
        src.pump_events();
    }
}

void cfile_unbuf(benchmark::State &state) {
    auto stream = internal::unbuffered_binary_cfile_input_stream("/dev/zero");
    auto const read_size = static_cast<std::size_t>(state.range(0));
    auto pool = std::make_shared<object_pool<std::vector<int>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto src =
            read_binary_stream<int>(ref_input_stream(stream), total_bytes,
                                    pool, read_size, unoptimized_null_sink());
        src.pump_events();
    }
}

void cfile(benchmark::State &state) {
    auto stream = internal::binary_cfile_input_stream("/dev/zero");
    auto const read_size = static_cast<std::size_t>(state.range(0));
    auto pool = std::make_shared<object_pool<std::vector<int>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto src =
            read_binary_stream<int>(ref_input_stream(stream), total_bytes,
                                    pool, read_size, unoptimized_null_sink());
        src.pump_events();
    }
}

// NOLINTBEGIN

BENCHMARK(ifstream_unbuf)->RangeMultiplier(2)->Range(4 << 10, 256 << 10);

BENCHMARK(ifstream)->RangeMultiplier(2)->Range(4 << 10, 256 << 10);

BENCHMARK(cfile_unbuf)->RangeMultiplier(2)->Range(4 << 10, 256 << 10);

BENCHMARK(cfile)->RangeMultiplier(2)->Range(4 << 10, 256 << 10);

// NOLINTEND

} // namespace tcspc

BENCHMARK_MAIN(); // NOLINT
