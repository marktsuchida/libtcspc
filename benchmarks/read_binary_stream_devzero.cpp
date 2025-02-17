/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/read_binary_stream.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/span.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace tcspc {

// Compare ifstream to C FILE *, buffering on or off, different read sizes.
// The optimum may also depend on downstream processing, which is no-op here.

// These benchmarks are good enough to conclude that unbuffered C files perform
// best. Finding the optimal read size requires testing a larger range (for
// large amounts of data (1 GiB), much larger read sizes (megabytes) were
// faster on an Apple M1 Pro laptop). It is probably also affected by what is
// done downstream.

// There is no /dev/zero equivalent on Windows.
#ifndef _WIN32

namespace {

// Access input stream via reference, to ensure stream creation is not
// dominant. (Effect was moderate for 1 MiB reads.)

// Exception escape likely a false positive here.
// NOLINTBEGIN(bugprone-exception-escape)
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
// NOLINTEND(bugprone-exception-escape)

class unoptimized_null_sink {
  public:
    template <typename Event> void handle(Event &&event) {
        benchmark::DoNotOptimize(std::forward<Event>(event));
    }

    static void flush(int x = 0) { benchmark::DoNotOptimize(x); }
};

constexpr auto zero_device = "/dev/zero";

constexpr std::size_t total_bytes = 1 << 20;

void ifstream_unbuf(benchmark::State &state) {
    auto stream = internal::unbuffered_binary_ifstream_input_stream(
        zero_device, arg::start_offset<u64>{0});
    auto const read_size = static_cast<std::size_t>(state.range(0));
    auto bsrc = recycling_bucket_source<int>::create();
    for ([[maybe_unused]] auto _ : state) {
        auto src = read_binary_stream<int>(
            ref_input_stream(stream), arg::max_length<u64>{total_bytes}, bsrc,
            arg::granularity{read_size}, unoptimized_null_sink());
        src.flush();
    }
}

void ifstream(benchmark::State &state) {
    auto stream = internal::binary_ifstream_input_stream(
        zero_device, arg::start_offset<u64>{0});
    auto const read_size = static_cast<std::size_t>(state.range(0));
    auto bsrc = recycling_bucket_source<int>::create();
    for ([[maybe_unused]] auto _ : state) {
        auto src = read_binary_stream<int>(
            ref_input_stream(stream), arg::max_length<u64>{total_bytes}, bsrc,
            arg::granularity{read_size}, unoptimized_null_sink());
        src.flush();
    }
}

void cfile_unbuf(benchmark::State &state) {
    auto stream = internal::unbuffered_binary_cfile_input_stream(
        zero_device, arg::start_offset<u64>{0});
    auto const read_size = static_cast<std::size_t>(state.range(0));
    auto bsrc = recycling_bucket_source<int>::create();
    for ([[maybe_unused]] auto _ : state) {
        auto src = read_binary_stream<int>(
            ref_input_stream(stream), arg::max_length<u64>{total_bytes}, bsrc,
            arg::granularity{read_size}, unoptimized_null_sink());
        src.flush();
    }
}

void cfile(benchmark::State &state) {
    auto stream = internal::binary_cfile_input_stream(
        zero_device, arg::start_offset<u64>{0});
    auto const read_size = static_cast<std::size_t>(state.range(0));
    auto bsrc = recycling_bucket_source<int>::create();
    for ([[maybe_unused]] auto _ : state) {
        auto src = read_binary_stream<int>(
            ref_input_stream(stream), arg::max_length<u64>{total_bytes}, bsrc,
            arg::granularity{read_size}, unoptimized_null_sink());
        src.flush();
    }
}

} // namespace

namespace benchmark {

// NOLINTBEGIN

constexpr auto start = 4 << 10;
constexpr auto limit =
#ifdef LIBTCSPC_ABRIDGE_BENCHMARKS
    start;
#else
    256 << 10;
#endif

BENCHMARK(ifstream_unbuf)->RangeMultiplier(2)->Range(start, limit);

BENCHMARK(ifstream)->RangeMultiplier(2)->Range(start, limit);

BENCHMARK(cfile_unbuf)->RangeMultiplier(2)->Range(start, limit);

BENCHMARK(cfile)->RangeMultiplier(2)->Range(start, limit);

// NOLINTEND

} // namespace benchmark

#endif // _WIN32

} // namespace tcspc

BENCHMARK_MAIN(); // NOLINT
