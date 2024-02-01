/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/write_binary_stream.hpp"

#include "libtcspc/autocopy_span.hpp"
#include "libtcspc/buffer.hpp"
#include "libtcspc/span.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace tcspc {

// Compare ofstream to C FILE *, buffering on or off, different write sizes.
// The optimum may also depend on upstream processing, which is no-op here.

// These benchmarks are good enough to conclude that unbuffered C files perform
// best. Finding the optimal write size requires testing a larger range (for
// large amounts of data (1 GiB), the overhead seemed to keep decreasing,
// although it starts to plateau at tens of megabytes, on an Apple M1 Pro
// laptop). It may also be affected by what is done upstream.

namespace {

// Access output stream via reference, to ensure stream creation is not
// dominant. (Effect was moderate for 1 MiB writes.)

// Exception escape likely a false positive here.
// NOLINTBEGIN(bugprone-exception-escape)
template <typename OutputStream> class ref_output_stream {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    OutputStream &stream;

  public:
    explicit ref_output_stream(OutputStream &stream) : stream(stream) {}

    auto is_error() noexcept -> bool { return stream.is_error(); }

    auto tell() noexcept -> std::optional<std::uint64_t> {
        return stream.tell();
    }

    void write(span<std::byte const> buffer) noexcept { stream.write(buffer); }
};
// NOLINTEND(bugprone-exception-escape)

constexpr std::size_t total_bytes = 1 << 20;

} // namespace

void ofstream_unbuf(benchmark::State &state) {
    auto stream = internal::unbuffered_binary_ofstream_output_stream(
        "/dev/null", false, true);
    auto const write_size = static_cast<std::size_t>(state.range(0));
    auto const num_writes = total_bytes / write_size;
    std::vector<std::byte> data(write_size);
    auto pool = std::make_shared<object_pool<std::vector<std::byte>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto proc =
            write_binary_stream(ref_output_stream(stream), pool, write_size);
        for (auto n = num_writes; n > 0; --n)
            proc.handle(autocopy_span(data));
    }
}

void ofstream(benchmark::State &state) {
    auto stream =
        internal::binary_ofstream_output_stream("/dev/null", false, true);
    auto const write_size = static_cast<std::size_t>(state.range(0));
    auto const num_writes = total_bytes / write_size;
    std::vector<std::byte> data(write_size);
    auto pool = std::make_shared<object_pool<std::vector<std::byte>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto proc =
            write_binary_stream(ref_output_stream(stream), pool, write_size);
        for (auto n = num_writes; n > 0; --n)
            proc.handle(autocopy_span(data));
    }
}

void cfile_unbuf(benchmark::State &state) {
    auto stream = internal::unbuffered_binary_cfile_output_stream("/dev/null",
                                                                  false, true);
    auto const write_size = static_cast<std::size_t>(state.range(0));
    auto const num_writes = total_bytes / write_size;
    std::vector<std::byte> data(write_size);
    auto pool = std::make_shared<object_pool<std::vector<std::byte>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto proc =
            write_binary_stream(ref_output_stream(stream), pool, write_size);
        for (auto n = num_writes; n > 0; --n)
            proc.handle(autocopy_span(data));
    }
}

void cfile(benchmark::State &state) {
    auto stream =
        internal::binary_cfile_output_stream("/dev/null", false, true);
    auto const write_size = static_cast<std::size_t>(state.range(0));
    auto const num_writes = total_bytes / write_size;
    std::vector<std::byte> data(write_size);
    auto pool = std::make_shared<object_pool<std::vector<std::byte>>>();
    for ([[maybe_unused]] auto _ : state) {
        auto proc =
            write_binary_stream(ref_output_stream(stream), pool, write_size);
        for (auto n = num_writes; n > 0; --n)
            proc.handle(autocopy_span(data));
    }
}

// NOLINTBEGIN

constexpr auto start = 4 << 10;
constexpr auto stop = 256 << 10;

BENCHMARK(ofstream_unbuf)->RangeMultiplier(2)->Range(start, stop);

BENCHMARK(ofstream)->RangeMultiplier(2)->Range(start, stop);

BENCHMARK(cfile_unbuf)->RangeMultiplier(2)->Range(start, stop);

BENCHMARK(cfile)->RangeMultiplier(2)->Range(start, stop);

// NOLINTEND

} // namespace tcspc

BENCHMARK_MAIN(); // NOLINT
