/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/read_binary_stream.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/bucket.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/core.hpp"
#include "libtcspc/int_types.hpp"
#include "libtcspc/processor_traits.hpp"
#include "libtcspc/stop.hpp"
#include "libtcspc/test_utils.hpp"
#include "libtcspc/type_list.hpp"
#include "test_checkers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>

namespace tcspc {

TEST_CASE("type constraints: read_binary_stream") {
    using proc_type = decltype(read_binary_stream<int>(
        null_input_stream(), arg::max_length<u64>{0},
        new_delete_bucket_source<int>::create(), arg::granularity<>{16},
        sink_events<bucket<int>, warning_event>()));
    STATIC_CHECK(is_processor_v<proc_type>);
    STATIC_CHECK_FALSE(handles_event_v<proc_type, int>);
}

TEST_CASE("introspect: read_binary_stream") {
    check_introspect_simple_processor(
        read_binary_stream<int>(null_input_stream(), arg::max_length<u64>{0},
                                new_delete_bucket_source<int>::create(),
                                arg::granularity<>{1}, null_sink()));
}

namespace {

constexpr auto ignore_value_category = feed_as::const_lvalue;

class autodelete {
    std::filesystem::path path;

  public:
    explicit autodelete(std::filesystem::path path) : path(std::move(path)) {}
    autodelete(autodelete const &) = delete;
    auto operator=(autodelete const &) = delete;
    autodelete(autodelete &&) = delete;
    auto operator=(autodelete &&) = delete;
    ~autodelete() { (void)std::remove(path.string().c_str()); }
};

} // namespace

TEST_CASE("read file") {
    auto path = std::filesystem::temp_directory_path() /
                "libtcspc_test_read_binary_stream";
    std::array<std::uint64_t, 7> data{42, 43, 44, 45, 46, 47, 48};

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    REQUIRE(stream.good());
    autodelete const autodel(path);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.write(reinterpret_cast<char *>(data.data()),
                 sizeof(std::uint64_t) * data.size());
    REQUIRE(stream.good());
    stream.close();

    auto ctx = context::create();

    SECTION("whole events") {
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), arg::start_offset<u64>{8}),
            arg::max_length<u64>{40},
            new_delete_bucket_source<std::uint64_t>::create(),
            arg::granularity<>{16},
            stop_with_error<type_list<warning_event>>(
                "read error",
                capture_output<type_list<bucket<std::uint64_t>>>(
                    ctx->tracker<capture_output_access>("out"))));
        auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
            ignore_value_category, ctx, "out");
        src.flush();
        // First read is 8 bytes to recover 16-byte aligned reads.
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({43})));
        REQUIRE(
            out.check(emitted_as::always_rvalue, test_bucket<u64>({44, 45})));
        REQUIRE(
            out.check(emitted_as::always_rvalue, test_bucket<u64>({46, 47})));
        REQUIRE(out.check_flushed());
    }

    SECTION("whole events, partial batch at end") {
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), arg::start_offset<u64>{8}),
            arg::max_length<u64>{48},
            new_delete_bucket_source<std::uint64_t>::create(),
            arg::granularity<>{16},
            stop_with_error<type_list<warning_event>>(
                "read error",
                capture_output<type_list<bucket<std::uint64_t>>>(
                    ctx->tracker<capture_output_access>("out"))));
        auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
            ignore_value_category, ctx, "out");
        src.flush();
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({43})));
        REQUIRE(
            out.check(emitted_as::always_rvalue, test_bucket<u64>({44, 45})));
        REQUIRE(
            out.check(emitted_as::always_rvalue, test_bucket<u64>({46, 47})));
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({48})));
        REQUIRE(out.check_flushed());
    }

    SECTION("extra bytes at end") {
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), arg::start_offset<u64>{8}),
            arg::max_length<u64>{44}, // 4 remainder bytes
            new_delete_bucket_source<std::uint64_t>::create(),
            arg::granularity<>{16},
            stop_with_error<type_list<warning_event>>(
                "read error",
                capture_output<type_list<bucket<std::uint64_t>>>(
                    ctx->tracker<capture_output_access>("out"))));
        auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
            ignore_value_category, ctx, "out");
        REQUIRE_THROWS_WITH(src.flush(),
                            Catch::Matchers::ContainsSubstring("remain"));
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({43})));
        REQUIRE(
            out.check(emitted_as::always_rvalue, test_bucket<u64>({44, 45})));
        REQUIRE(
            out.check(emitted_as::always_rvalue, test_bucket<u64>({46, 47})));
        REQUIRE(out.check_not_flushed());
    }

    SECTION("read size smaller than event size") {
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), arg::start_offset<u64>{8}),
            arg::max_length<u64>{40},
            new_delete_bucket_source<std::uint64_t>::create(),
            arg::granularity<>{3},
            stop_with_error<type_list<warning_event>>(
                "read error",
                capture_output<type_list<bucket<std::uint64_t>>>(
                    ctx->tracker<capture_output_access>("out"))));
        auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
            ignore_value_category, ctx, "out");
        src.flush();
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({43})));
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({44})));
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({45})));
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({46})));
        REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({47})));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("read existing istream, known length") {
    std::array<std::uint64_t, 7> data{42, 43, 44, 45, 46, 47, 48};
    std::stringstream stream;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.write(reinterpret_cast<char *>(data.data()),
                 sizeof(std::uint64_t) * data.size());
    REQUIRE(stream.good());

    auto ctx = context::create();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream), arg::max_length<u64>{40},
        new_delete_bucket_source<std::uint64_t>::create(),
        arg::granularity<>{16},
        stop_with_error<type_list<warning_event>>(
            "read error", capture_output<type_list<bucket<std::uint64_t>>>(
                              ctx->tracker<capture_output_access>("out"))));
    auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
        ignore_value_category, ctx, "out");
    src.flush();
    REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({42, 43})));
    REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({44, 45})));
    REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({46})));
    REQUIRE(out.check_flushed());
}

TEST_CASE("read existing istream, to end") {
    std::array<std::uint64_t, 7> data{42, 43, 44, 45, 46, 47, 48};
    std::stringstream stream;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.write(reinterpret_cast<char *>(data.data()),
                 sizeof(std::uint64_t) * data.size());
    REQUIRE(stream.good());

    auto ctx = context::create();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream),
        arg::max_length{std::numeric_limits<std::uint64_t>::max()},
        new_delete_bucket_source<std::uint64_t>::create(),
        arg::granularity<>{16},
        stop_with_error<type_list<warning_event>>(
            "read error", capture_output<type_list<bucket<std::uint64_t>>>(
                              ctx->tracker<capture_output_access>("out"))));
    auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
        ignore_value_category, ctx, "out");
    src.flush();
    REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({42, 43})));
    REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({44, 45})));
    REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({46, 47})));
    REQUIRE(out.check(emitted_as::always_rvalue, test_bucket<u64>({48})));
    REQUIRE(out.check_flushed());
}

TEST_CASE("read existing istream, empty") {
    std::istringstream stream;
    REQUIRE(stream.good());

    auto ctx = context::create();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream),
        arg::max_length{std::numeric_limits<std::uint64_t>::max()},
        new_delete_bucket_source<std::uint64_t>::create(),
        arg::granularity<>{16},
        stop_with_error<type_list<warning_event>>(
            "read error", capture_output<type_list<bucket<std::uint64_t>>>(
                              ctx->tracker<capture_output_access>("out"))));
    auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
        ignore_value_category, ctx, "out");
    src.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
