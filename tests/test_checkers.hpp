/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <regex>
#include <string>

namespace tcspc {

namespace internal {

inline void check_type_name(std::string const &type_name,
                            std::string const &name) {
    // With unknown compiler/platform, there is no guarantee about type_name.
#if defined(__GNUC__) || defined(_MSC_VER)
    // Type name should begin with tcspc::[internal::] followed by name,
    // followed by either '<' or end of string.
    std::array const res = {
        std::regex("^tcspc::internal::" + name + "<"),
        std::regex("^tcspc::internal::" + name + "$"),
        std::regex("^tcspc::" + name + "<"),
        std::regex("^tcspc::" + name + "$"),
    };
    CHECK(std::any_of(res.begin(), res.end(), [&](auto const &re) {
        return std::regex_search(type_name, re);
    }));
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

template <typename Source>
inline auto
check_introspect_simple_source(Source const &source_with_null_sink) {
    auto const info = check_introspect_node_info(source_with_null_sink);

    auto const g = source_with_null_sink.introspect_graph();
    CHECK(g.nodes().size() == 2);
    CHECK(g.entry_points().size() == 0);
    CHECK(g.edges().size() == 1);
    auto const node = g.edges()[0].first;
    CHECK(g.node_info(node) == info);
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
