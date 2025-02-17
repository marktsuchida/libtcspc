/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace tcspc {

namespace internal {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline void check_type_name([[maybe_unused]] std::string type_name,
                            [[maybe_unused]] std::string const &name) {
    // With unknown compiler/platform, there is no guarantee about type_name.
    // We only check for known compilers.

    // Type name should begin with tcspc::[internal::] followed by name,
    // followed by either '<' or end of string. MSVC adds "class" or "struct"
    // before the type name.

#if defined(__GNUC__) || defined(_MSC_VER)
    auto startswith = [](std::string_view str, std::string_view prefix) {
        return str.rfind(prefix, 0) == 0;
    };
    auto remove_prefix = [&](std::string &str, std::string_view prefix) {
        return str = str.substr(prefix.size());
    };

#if defined(_MSC_VER)
    if (startswith(type_name, "class "))
        remove_prefix(type_name, "class ");
    else if (startswith(type_name, "struct "))
        remove_prefix(type_name, "struct ");
#endif

    REQUIRE(startswith(type_name, "tcspc::"));
    remove_prefix(type_name, "tcspc::");
    if (startswith(type_name, "internal::"))
        remove_prefix(type_name, "internal::");
    REQUIRE(startswith(type_name, name));
    remove_prefix(type_name, name);
    if (not type_name.empty())
        REQUIRE(startswith(type_name, "<"));
#endif
}

} // namespace internal

template <typename Processor>
inline auto check_introspect_node_info(Processor const &proc) {
    auto const info = proc.introspect_node();
    CHECK(info.address() == std::size_t(&proc));
    internal::check_type_name(info.type_name(), info.name());
    return info;
}

template <typename Processor>
inline auto
check_introspect_simple_processor(Processor const &processor_with_null_sink) {
    auto const info = check_introspect_node_info(processor_with_null_sink);

    auto const g = processor_with_null_sink.introspect_graph();
    CHECK(g.nodes().size() == 2);
    CHECK(g.entry_points().size() == 1);
    auto const node = g.entry_points()[0];
    CHECK(g.node_info(node) == info);
    CHECK(g.edges().size() == 1);
    CHECK(g.edges()[0].first == node);
    auto const downstream_node = g.edges()[0].second;
    CHECK(g.node_info(downstream_node).name() == "null_sink");
    return info;
}

template <typename Sink>
inline auto check_introspect_simple_sink(Sink const &sink) {
    auto const info = check_introspect_node_info(sink);

    auto const g = sink.introspect_graph();
    CHECK(g.nodes().size() == 1);
    auto const node = g.nodes()[0];
    CHECK(g.is_entry_point(node));
    CHECK(g.entry_points().size() == 1);
    CHECK(g.node_info(node) == info);
    CHECK(g.edges().size() == 0);
    return info;
}

} // namespace tcspc
