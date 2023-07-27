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

namespace {

class unoptimized_null_sink {
  public:
    template <typename Event> void handle_event(Event const &event) noexcept {
        benchmark::DoNotOptimize(event);
    }

    static void handle_end(std::exception_ptr const &error) noexcept {
        benchmark::DoNotOptimize(error);
    }
};

static constexpr std::size_t total_bytes = 1 << 20;

} // namespace

void ifstream_unbuf(benchmark::State &state) {
    auto pool = std::make_shared<object_pool<std::vector<int>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto stream =
            internal::unbuffered_binary_ifstream_input_stream("/dev/zero");
        auto src =
            read_binary_stream<int>(std::move(stream), total_bytes, pool,
                                    state.range(0), unoptimized_null_sink());
        src.pump_events();
    }
}

void ifstream(benchmark::State &state) {
    auto pool = std::make_shared<object_pool<std::vector<int>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto stream = internal::binary_ifstream_input_stream("/dev/zero");
        auto src =
            read_binary_stream<int>(std::move(stream), total_bytes, pool,
                                    state.range(0), unoptimized_null_sink());
        src.pump_events();
    }
}

void cfile_unbuf(benchmark::State &state) {
    auto pool = std::make_shared<object_pool<std::vector<int>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto stream =
            internal::unbuffered_binary_cfile_input_stream("/dev/zero");
        auto src =
            read_binary_stream<int>(std::move(stream), total_bytes, pool,
                                    state.range(0), unoptimized_null_sink());
        src.pump_events();
    }
}

void cfile(benchmark::State &state) {
    auto pool = std::make_shared<object_pool<std::vector<int>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto stream = internal::binary_cfile_input_stream("/dev/zero");
        auto src =
            read_binary_stream<int>(std::move(stream), total_bytes, pool,
                                    state.range(0), unoptimized_null_sink());
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
