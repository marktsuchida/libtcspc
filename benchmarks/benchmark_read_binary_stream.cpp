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

// Compare ifstream to C FILE *. For now this requires /dev/zero and won't
// perform actual reads on Windows (need to create a temporary file).

// Also compare different read sizes. The optimum may also depend on downstream
// processing, which is no-op here.

namespace {

class unoptimized_null_sink {
  public:
    template <typename Event> void handle_event(Event const &event) noexcept {
        benchmark::DoNotOptimize(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        benchmark::DoNotOptimize(error);
    }
};

} // namespace

void bm_read_devzero_ifstream_1M_unbuf(benchmark::State &state) {
    for (auto _ : state) {
        auto stream =
            internal::unbuffered_binary_ifstream_input_stream("/dev/zero");
        auto src = read_binary_stream<int>(
            std::move(stream), 1024 * 1024,
            std::make_shared<object_pool<std::vector<int>>>(), state.range(0),
            unoptimized_null_sink());
        src.pump_events();
    }
}

void bm_read_devzero_ifstream_1M(benchmark::State &state) {
    for (auto _ : state) {
        auto stream = internal::binary_ifstream_input_stream("/dev/zero");
        auto src = read_binary_stream<int>(
            std::move(stream), 1024 * 1024,
            std::make_shared<object_pool<std::vector<int>>>(), state.range(0),
            unoptimized_null_sink());
        src.pump_events();
    }
}

void bm_read_devzero_cfile_1M_unbuf(benchmark::State &state) {
    for (auto _ : state) {
        auto stream =
            internal::unbuffered_binary_cfile_input_stream("/dev/zero");
        auto src = read_binary_stream<int>(
            std::move(stream), 1024 * 1024,
            std::make_shared<object_pool<std::vector<int>>>(), state.range(0),
            unoptimized_null_sink());
        src.pump_events();
    }
}

void bm_read_devzero_cfile_1M(benchmark::State &state) {
    for (auto _ : state) {
        auto stream = internal::binary_cfile_input_stream("/dev/zero");
        auto src = read_binary_stream<int>(
            std::move(stream), 1024 * 1024,
            std::make_shared<object_pool<std::vector<int>>>(), state.range(0),
            unoptimized_null_sink());
        src.pump_events();
    }
}

BENCHMARK(bm_read_devzero_ifstream_1M_unbuf)
    ->RangeMultiplier(2)
    ->Range(4 * 1024, 256 * 1024);

BENCHMARK(bm_read_devzero_ifstream_1M)
    ->RangeMultiplier(2)
    ->Range(4 * 1024, 256 * 1024);

BENCHMARK(bm_read_devzero_cfile_1M_unbuf)
    ->RangeMultiplier(2)
    ->Range(4 * 1024, 256 * 1024);

BENCHMARK(bm_read_devzero_cfile_1M)
    ->RangeMultiplier(2)
    ->Range(4 * 1024, 256 * 1024);

} // namespace tcspc

BENCHMARK_MAIN();
