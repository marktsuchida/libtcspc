/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/vector_queue.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <queue>

namespace tcspc::internal {

namespace {

template <typename Q> void push_read_pop(benchmark::State &state) {
    Q q;
    auto const len = state.range(0);
    for ([[maybe_unused]] auto _ : state) {
        for (std::int64_t i = 0; i < len; ++i)
            q.push(i);
        while (not q.empty()) {
            benchmark::DoNotOptimize(q.front());
            q.pop();
        }
    }
}

void vector_queue_push_read_pop(benchmark::State &state) {
    push_read_pop<vector_queue<std::int64_t>>(state);
}

void std_queue_push_read_pop(benchmark::State &state) {
    push_read_pop<std::queue<std::int64_t>>(state);
}

} // namespace

namespace benchmark {

// NOLINTBEGIN

constexpr auto start = 1;
constexpr auto limit =
#ifdef LIBTCSPC_ABRIDGE_BENCHMARKS
    start;
#else
    512;
#endif

BENCHMARK(vector_queue_push_read_pop)->RangeMultiplier(4)->Range(start, limit);

BENCHMARK(std_queue_push_read_pop)->RangeMultiplier(4)->Range(start, limit);

// NOLINTEND

} // namespace benchmark

} // namespace tcspc::internal

BENCHMARK_MAIN(); // NOLINT
