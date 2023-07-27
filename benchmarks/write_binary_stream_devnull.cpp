/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/write_binary_stream.hpp"

#include "libtcspc/common.hpp"

#include <benchmark/benchmark.h>

#include <memory>
#include <vector>

namespace tcspc {

// Compare ofstream to C FILE *, buffering on or off, different write sizes.
// The optimum may also depend on upstream processing, which is no-op here.

namespace {

static constexpr std::size_t total_bytes = 1 << 20;

}

void ofstream_unbuf(benchmark::State &state) {
    auto const write_size = state.range(0);
    auto const num_writes = total_bytes / write_size;
    std::vector<std::byte> data(write_size);
    auto pool = std::make_shared<object_pool<std::vector<std::byte>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto stream =
            internal::unbuffered_binary_ofstream_output_stream("/dev/null");
        auto proc = write_binary_stream(std::move(stream), pool, write_size,
                                        null_sink());
        for (auto n = num_writes; n > 0; --n)
            proc.handle_event(autocopy_span(data));
    }
}

void ofstream(benchmark::State &state) {
    auto const write_size = state.range(0);
    auto const num_writes = total_bytes / write_size;
    std::vector<std::byte> data(write_size);
    auto pool = std::make_shared<object_pool<std::vector<std::byte>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto stream = internal::binary_ofstream_output_stream("/dev/null");
        auto proc = write_binary_stream(std::move(stream), pool, write_size,
                                        null_sink());
        for (auto n = num_writes; n > 0; --n)
            proc.handle_event(autocopy_span(data));
    }
}

void cfile_unbuf(benchmark::State &state) {
    auto const write_size = state.range(0);
    auto const num_writes = total_bytes / write_size;
    std::vector<std::byte> data(write_size);
    auto pool = std::make_shared<object_pool<std::vector<std::byte>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto stream =
            internal::unbuffered_binary_cfile_output_stream("/dev/null");
        auto proc = write_binary_stream(std::move(stream), pool, write_size,
                                        null_sink());
        for (auto n = num_writes; n > 0; --n)
            proc.handle_event(autocopy_span(data));
    }
}

void cfile(benchmark::State &state) {
    auto const write_size = state.range(0);
    auto const num_writes = total_bytes / write_size;
    std::vector<std::byte> data(write_size);
    auto pool = std::make_shared<object_pool<std::vector<std::byte>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto stream = internal::binary_cfile_output_stream("/dev/null");
        auto proc = write_binary_stream(std::move(stream), pool, write_size,
                                        null_sink());
        for (auto n = num_writes; n > 0; --n)
            proc.handle_event(autocopy_span(data));
    }
}

// NOLINTBEGIN

BENCHMARK(ofstream_unbuf)->RangeMultiplier(2)->Range(4 << 10, 256 << 10);

BENCHMARK(ofstream)->RangeMultiplier(2)->Range(4 << 10, 256 << 10);

BENCHMARK(cfile_unbuf)->RangeMultiplier(2)->Range(4 << 10, 256 << 10);

BENCHMARK(cfile)->RangeMultiplier(2)->Range(4 << 10, 256 << 10);

// NOLINTEND

} // namespace tcspc

BENCHMARK_MAIN(); // NOLINT
