/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/read_binary_stream.hpp"

#include "libtcspc/bucket.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/processor_context.hpp"
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
#include <vector>

namespace tcspc {

TEST_CASE("introspect read_binary_stream", "[introspect]") {
    check_introspect_simple_source(read_binary_stream<int>(
        null_input_stream(), 0, new_delete_bucket_source<int>::create(), 1,
        null_sink()));
}

namespace {

class autodelete {
    std::filesystem::path path;

  public:
    explicit autodelete(std::filesystem::path path) : path(std::move(path)) {}
    autodelete(autodelete const &) = delete;
    auto operator=(autodelete const &) = delete;
    autodelete(autodelete &&) = delete;
    auto operator=(autodelete &&) = delete;
    ~autodelete() { std::remove(path.string().c_str()); }
};

template <typename T, typename U>
auto tmp_bucket(std::initializer_list<U> il) {
    static auto src = new_delete_bucket_source<T>::create();
    auto b = src->bucket_of_size(il.size());
    std::copy(il.begin(), il.end(), b.begin());
    return b;
}

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

    auto ctx = std::make_shared<processor_context>();

    SECTION("whole events") {
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), 8), 40,
            new_delete_bucket_source<std::uint64_t>::create(), 16,
            stop_with_error<type_list<warning_event>>(
                "read error",
                capture_output<type_list<bucket<std::uint64_t>>>(
                    ctx->tracker<capture_output_access>("out"))));
        auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
            ctx->access<capture_output_access>("out"));
        src.flush();
        // First read is 8 bytes to recover 16-byte aligned reads.
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({43})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({44, 45})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({46, 47})));
        REQUIRE(out.check_flushed());
    }

    SECTION("whole events, partial batch at end") {
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), 8), 48,
            new_delete_bucket_source<std::uint64_t>::create(), 16,
            stop_with_error<type_list<warning_event>>(
                "read error",
                capture_output<type_list<bucket<std::uint64_t>>>(
                    ctx->tracker<capture_output_access>("out"))));
        auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
            ctx->access<capture_output_access>("out"));
        src.flush();
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({43})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({44, 45})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({46, 47})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({48})));
        REQUIRE(out.check_flushed());
    }

    SECTION("extra bytes at end") {
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), 8),
            44, // 4 remainder bytes
            new_delete_bucket_source<std::uint64_t>::create(), 16,
            stop_with_error<type_list<warning_event>>(
                "read error",
                capture_output<type_list<bucket<std::uint64_t>>>(
                    ctx->tracker<capture_output_access>("out"))));
        auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
            ctx->access<capture_output_access>("out"));
        REQUIRE_THROWS_WITH(src.flush(),
                            Catch::Matchers::ContainsSubstring("remain"));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({43})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({44, 45})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({46, 47})));
        REQUIRE(out.check_not_flushed());
    }

    SECTION("read size smaller than event size") {
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), 8), 40,
            new_delete_bucket_source<std::uint64_t>::create(), 3,
            stop_with_error<type_list<warning_event>>(
                "read error",
                capture_output<type_list<bucket<std::uint64_t>>>(
                    ctx->tracker<capture_output_access>("out"))));
        auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
            ctx->access<capture_output_access>("out"));
        src.flush();
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({43})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({44})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({45})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({46})));
        REQUIRE(out.check(tmp_bucket<std::uint64_t>({47})));
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

    auto ctx = std::make_shared<processor_context>();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream), 40,
        new_delete_bucket_source<std::uint64_t>::create(), 16,
        stop_with_error<type_list<warning_event>>(
            "read error", capture_output<type_list<bucket<std::uint64_t>>>(
                              ctx->tracker<capture_output_access>("out"))));
    auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
        ctx->access<capture_output_access>("out"));
    src.flush();
    REQUIRE(out.check(tmp_bucket<std::uint64_t>({42, 43})));
    REQUIRE(out.check(tmp_bucket<std::uint64_t>({44, 45})));
    REQUIRE(out.check(tmp_bucket<std::uint64_t>({46})));
    REQUIRE(out.check_flushed());
}

TEST_CASE("read existing istream, to end") {
    std::array<std::uint64_t, 7> data{42, 43, 44, 45, 46, 47, 48};
    std::stringstream stream;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.write(reinterpret_cast<char *>(data.data()),
                 sizeof(std::uint64_t) * data.size());
    REQUIRE(stream.good());

    auto ctx = std::make_shared<processor_context>();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream), std::numeric_limits<std::uint64_t>::max(),
        new_delete_bucket_source<std::uint64_t>::create(), 16,
        stop_with_error<type_list<warning_event>>(
            "read error", capture_output<type_list<bucket<std::uint64_t>>>(
                              ctx->tracker<capture_output_access>("out"))));
    auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
        ctx->access<capture_output_access>("out"));
    src.flush();
    REQUIRE(out.check(tmp_bucket<std::uint64_t>({42, 43})));
    REQUIRE(out.check(tmp_bucket<std::uint64_t>({44, 45})));
    REQUIRE(out.check(tmp_bucket<std::uint64_t>({46, 47})));
    REQUIRE(out.check(tmp_bucket<std::uint64_t>({48})));
    REQUIRE(out.check_flushed());
}

TEST_CASE("read existing istream, empty") {
    std::istringstream stream;
    REQUIRE(stream.good());

    auto ctx = std::make_shared<processor_context>();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream), std::numeric_limits<std::uint64_t>::max(),
        new_delete_bucket_source<std::uint64_t>::create(), 16,
        stop_with_error<type_list<warning_event>>(
            "read error", capture_output<type_list<bucket<std::uint64_t>>>(
                              ctx->tracker<capture_output_access>("out"))));
    auto out = capture_output_checker<type_list<bucket<std::uint64_t>>>(
        ctx->access<capture_output_access>("out"));
    src.flush();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
