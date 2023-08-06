/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/read_binary_stream.hpp"

#include "libtcspc/ref_processor.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/stop.hpp"
#include "libtcspc/test_utils.hpp"

#include <catch2/catch_all.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tcspc {

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

} // namespace

TEST_CASE("read file", "[read_binary_stream]") {
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

    SECTION("whole events") {
        auto out = capture_output<event_set<pvector<std::uint64_t>>>();
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), 8), 40,
            std::make_shared<object_pool<pvector<std::uint64_t>>>(), 16,
            stop_with_error<event_set<warning_event>>(
                "read error",
                dereference_pointer<std::shared_ptr<pvector<std::uint64_t>>>(
                    ref_processor(out))));
        src.pump_events();
        // First read is 8 bytes to recover 16-byte aligned reads.
        REQUIRE(out.check(pvector<std::uint64_t>{43}));
        REQUIRE(out.check(pvector<std::uint64_t>{44, 45}));
        REQUIRE(out.check(pvector<std::uint64_t>{46, 47}));
        REQUIRE(out.check_flushed());
    }

    SECTION("whole events, partial batch at end") {
        auto out = capture_output<event_set<pvector<std::uint64_t>>>();
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), 8), 48,
            std::make_shared<object_pool<pvector<std::uint64_t>>>(), 16,
            stop_with_error<event_set<warning_event>>(
                "read error",
                dereference_pointer<std::shared_ptr<pvector<std::uint64_t>>>(
                    ref_processor(out))));
        src.pump_events();
        REQUIRE(out.check(pvector<std::uint64_t>{43}));
        REQUIRE(out.check(pvector<std::uint64_t>{44, 45}));
        REQUIRE(out.check(pvector<std::uint64_t>{46, 47}));
        REQUIRE(out.check(pvector<std::uint64_t>{48}));
        REQUIRE(out.check_flushed());
    }

    SECTION("extra bytes at end") {
        auto out = capture_output<event_set<pvector<std::uint64_t>>>();
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), 8),
            44, // 4 remainder bytes
            std::make_shared<object_pool<pvector<std::uint64_t>>>(), 16,
            stop_with_error<event_set<warning_event>>(
                "read error",
                dereference_pointer<std::shared_ptr<pvector<std::uint64_t>>>(
                    ref_processor(out))));
        REQUIRE_THROWS_WITH(src.pump_events(),
                            Catch::Matchers::ContainsSubstring("remain"));
        REQUIRE(out.check(pvector<std::uint64_t>{43}));
        REQUIRE(out.check(pvector<std::uint64_t>{44, 45}));
        REQUIRE(out.check(pvector<std::uint64_t>{46, 47}));
        REQUIRE(out.check_not_flushed());
    }

    SECTION("read size smaller than event size") {
        auto out = capture_output<event_set<pvector<std::uint64_t>>>();
        auto src = read_binary_stream<std::uint64_t>(
            binary_file_input_stream(path.string(), 8), 40,
            std::make_shared<object_pool<pvector<std::uint64_t>>>(), 3,
            stop_with_error<event_set<warning_event>>(
                "read error",
                dereference_pointer<std::shared_ptr<pvector<std::uint64_t>>>(
                    ref_processor(out))));
        src.pump_events();
        REQUIRE(out.check(pvector<std::uint64_t>{43}));
        REQUIRE(out.check(pvector<std::uint64_t>{44}));
        REQUIRE(out.check(pvector<std::uint64_t>{45}));
        REQUIRE(out.check(pvector<std::uint64_t>{46}));
        REQUIRE(out.check(pvector<std::uint64_t>{47}));
        REQUIRE(out.check_flushed());
    }
}

TEST_CASE("read existing istream, known length", "[read_binary_stream]") {
    std::array<std::uint64_t, 7> data{42, 43, 44, 45, 46, 47, 48};
    std::stringstream stream;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.write(reinterpret_cast<char *>(data.data()),
                 sizeof(std::uint64_t) * data.size());
    REQUIRE(stream.good());

    auto out = capture_output<event_set<pvector<std::uint64_t>>>();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream), 40,
        std::make_shared<object_pool<pvector<std::uint64_t>>>(), 16,
        stop_with_error<event_set<warning_event>>(
            "read error",
            dereference_pointer<std::shared_ptr<pvector<std::uint64_t>>>(
                ref_processor(out))));
    src.pump_events();
    REQUIRE(out.check(pvector<std::uint64_t>{42, 43}));
    REQUIRE(out.check(pvector<std::uint64_t>{44, 45}));
    REQUIRE(out.check(pvector<std::uint64_t>{46}));
    REQUIRE(out.check_flushed());
}

TEST_CASE("read existing istream, to end", "[read_binary_stream]") {
    std::array<std::uint64_t, 7> data{42, 43, 44, 45, 46, 47, 48};
    std::stringstream stream;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.write(reinterpret_cast<char *>(data.data()),
                 sizeof(std::uint64_t) * data.size());
    REQUIRE(stream.good());

    auto out = capture_output<event_set<pvector<std::uint64_t>>>();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream), std::numeric_limits<std::uint64_t>::max(),
        std::make_shared<object_pool<pvector<std::uint64_t>>>(), 16,
        stop_with_error<event_set<warning_event>>(
            "read error",
            dereference_pointer<std::shared_ptr<pvector<std::uint64_t>>>(
                ref_processor(out))));
    src.pump_events();
    REQUIRE(out.check(pvector<std::uint64_t>{42, 43}));
    REQUIRE(out.check(pvector<std::uint64_t>{44, 45}));
    REQUIRE(out.check(pvector<std::uint64_t>{46, 47}));
    REQUIRE(out.check(pvector<std::uint64_t>{48}));
    REQUIRE(out.check_flushed());
}

TEST_CASE("read existing istream, empty", "[read_binary_stream]") {
    std::istringstream stream;
    REQUIRE(stream.good());

    auto out = capture_output<event_set<pvector<std::uint64_t>>>();
    auto src = read_binary_stream<std::uint64_t>(
        std::move(stream), std::numeric_limits<std::uint64_t>::max(),
        std::make_shared<object_pool<pvector<std::uint64_t>>>(), 16,
        stop_with_error<event_set<warning_event>>(
            "read error",
            dereference_pointer<std::shared_ptr<pvector<std::uint64_t>>>(
                ref_processor(out))));
    src.pump_events();
    REQUIRE(out.check_flushed());
}

} // namespace tcspc
