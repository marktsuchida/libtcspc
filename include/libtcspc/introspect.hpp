/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

#ifdef __GNUC__
#if __has_include(<cxxabi.h>)
#define LIBTCSPC_HAVE_CXA_DEMANGLE
#include <cxxabi.h>
#else
#warning introspected type names will be mangled (cxxabi.h not available)
#endif
#endif

namespace tcspc {

namespace internal {

[[nodiscard]] inline auto maybe_demangle(char const *mangled) -> std::string {
#ifdef LIBTCSPC_HAVE_CXA_DEMANGLE
    int status{-1};
    auto demangled = std::unique_ptr<char, void (*)(void *)>(
        abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
    return status == 0 ? demangled.get() : mangled;
#else
    return mangled;
#endif
}

} // namespace internal

/**
 * \brief Value type representing metadata of a processor.
 *
 * \ingroup introspect
 *
 * Each processor (or source, sink) implements a member function \c
 * introspect_node() that returns an object of this type. The same result can
 * also be obtained for nodes of a \ref processor_graph.
 */
class processor_info {
    void const *addr;
    std::type_index typ;
    std::string nm;

  public:
    /**
     * \brief Construct with pointer to processor and name.
     *
     * \tparam Processor the processor type (usually deduced)
     *
     * \param processor pointer to the processor
     *
     * \param name name of the processor (by convention, usually the
     * unqualified name of the class or class template, without a template
     * argument list)
     */
    template <typename Processor>
    explicit processor_info(Processor const *processor, std::string name)
        : addr(processor), typ(typeid(Processor)), nm(std::move(name)) {}

    /**
     * \brief Return the address of the processor.
     *
     * This is for debugging or disambiguating purposes, not intended to be
     * used as a pointer.
     *
     * \return address of processor
     */
    [[nodiscard]] auto address() const -> std::size_t {
        return std::size_t(addr);
    }

    /**
     * \brief Return the C++ type name of the processor.
     *
     * Where possible, the demangled C++ type name is returned; otherwise the
     * returned name is whatever the platform's \c std::type_info::name()
     * returns.
     *
     * Processor type names can be quite long when they have a chain of
     * downstream processors.
     *
     * \return name of processor class
     */
    [[nodiscard]] auto type_name() const -> std::string {
        return internal::maybe_demangle(typ.name());
    }

    /**
     * \brief Return the simple name of the processor.
     *
     * \return name of processor
     */
    [[nodiscard]] auto name() const -> std::string { return nm; }

    /** \brief Equality comparison operator. */
    friend auto operator==(processor_info const &lhs,
                           processor_info const &rhs) -> bool {
        return lhs.addr == rhs.addr && lhs.typ == rhs.typ && lhs.nm == rhs.nm;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(processor_info const &lhs,
                           processor_info const &rhs) -> bool {
        return not(lhs == rhs);
    }
};

/**
 * \brief Value type representing processor identity within a graph.
 *
 * \ingroup introspect
 *
 * This type is used to represent a node in a processor_graph. Usually
 * instances are obtained through methods or processor_graph.
 *
 * The mapping between a processor_node_id value and a processor instance is
 * bijective, provided that the processor is not moved or destroyed.
 *
 * It is not possible to generate a concise and unique name for a node from the
 * processor_node_id alone; this requires the context of a processor_graph.
 */
class processor_node_id {
    // The processor address is not sufficient as a unique id, because some
    // processors have their downstream as the first data member (resulting in
    // the downstream having the same address). Pairing with the type id fixes
    // this issue, because a data member cannot have the same type as its
    // containing class.
    std::size_t addr;
    std::type_index typ;

  public:
    /**
     * \brief Construct with a pointer to a processor.
     *
     * \tparam Processor processor type (usually deduced)
     *
     * \param processor pointer to the processor
     */
    template <typename Processor>
    explicit processor_node_id(Processor const *processor)
        : addr(std::size_t(processor)), typ(typeid(Processor)) {}

    /** \brief Equality comparison operator. */
    friend auto operator==(processor_node_id const &lhs,
                           processor_node_id const &rhs) -> bool {
        return lhs.addr == rhs.addr && lhs.typ == rhs.typ;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(processor_node_id const &lhs,
                           processor_node_id const &rhs) -> bool {
        return not(lhs == rhs);
    }

    /** \brief Less-than operator. */
    friend auto operator<(processor_node_id const &lhs,
                          processor_node_id const &rhs) -> bool {
        return lhs.addr < rhs.addr ||
               (lhs.addr == rhs.addr && lhs.typ < rhs.typ);
    }

    /** \brief Greater-than operator. */
    friend auto operator>(processor_node_id const &lhs,
                          processor_node_id const &rhs) -> bool {
        return lhs.addr > rhs.addr ||
               (lhs.addr == rhs.addr && lhs.typ > rhs.typ);
    }

    /** \brief Less-than-or-equal-to operator. */
    friend auto operator<=(processor_node_id const &lhs,
                           processor_node_id const &rhs) -> bool {
        return not(lhs > rhs);
    }

    /** \brief Greater-than-or-equal-to operator. */
    friend auto operator>=(processor_node_id const &lhs,
                           processor_node_id const &rhs) -> bool {
        return not(lhs < rhs);
    }
};

/**
 * \brief Value type representing a directed acyclic graph of processors.
 *
 * \ingroup introspect
 *
 * Each processor (or source/sink) implements a member function \c
 * introspect_graph() that returns an instance representing the processor and
 * all of its downstream nodes.
 *
 * The graph includes the notion of "entry points" in addition to nodes and
 * (directed) edges. The entry points are the upstream-most processors
 * represented in the graph.
 *
 * The graph and its nodes are pure data types, so that they remain valid even
 * after the processors from which they were constructed are moved or
 * destroyed.
 *
 * Note that the proceessor graph exposes implementation details that are not
 * stable (even once we have a stable API). It is intended primarily for
 * visualization, debugging, and testing, not as a basis for automation.
 */
class processor_graph {
    struct node_type {
        processor_node_id id;
        processor_info info;
    };

    // All vectors kept sorted individually.
    std::vector<node_type> nds;
    std::vector<std::pair<processor_node_id, processor_node_id>> edgs;
    std::vector<processor_node_id> entrypts;

  public:
    /**
     * \brief Add a processor node to this graph, upstream of the current entry
     * point (if any), making it the new entry point.
     *
     * \pre The graph must have no more than 1 entry point. The processor must
     * not already be part of the graph.
     *
     * \post A new node is added to the graph, representing the given
     * processor, and made the sole entry point. If there was a previous entry
     * point, an edge is added to the graph from the new node to the previous
     * entry point.
     *
     * \tparam Processor processor type (usually deduced)
     *
     * \param processor pointer to the processor
     */
    template <typename Processor>
    void push_entry_point(Processor const *processor) {
        if (entrypts.size() > 1) {
            throw std::logic_error(
                "processor_graph can only push entry point when it has at most one entry point");
        }

        auto const id = processor_node_id(processor);
        auto node = node_type{id, processor->introspect_node()};
        auto [node_lower, node_upper] = std::equal_range(
            nds.begin(), nds.end(), node,
            [](auto const &l, auto const &r) { return l.id < r.id; });
        if (node_lower != node_upper) {
            throw std::logic_error(
                "processor_graph cannot push entry point that already exists");
        }
        nds.insert(node_upper, std::move(node));

        if (entrypts.empty()) {
            entrypts.push_back(id);
        } else {
            auto const edge = std::pair{id, entrypts[0]};
            auto edge_inspt = std::upper_bound(edgs.begin(), edgs.end(), edge);
            edgs.insert(edge_inspt, edge);
            entrypts[0] = id;
        }
    }

    /**
     * \brief Add a source node to this graph, upstream of the current entry
     * point (if any).
     *
     * \pre The graph must have no more than 1 entry point. The source must not
     * already be part of the graph.
     *
     * \post A new node is added to the graph, representing the given
     * source. The graph is set to have no entry point. If there was a previous
     * entry point, an edge is added to the graph from the new node to the
     * previous entry point.
     *
     * \tparam Source source type (usually deduced)
     *
     * \param source pointer to the source
     */
    template <typename Source> void push_source(Source const *source) {
        push_entry_point(source);
        entrypts.clear();
    }

    /**
     * \brief Return all of the nodes of this graph.
     *
     * \return node ids, sorted in ascending order
     */
    [[nodiscard]] auto nodes() const -> std::vector<processor_node_id> {
        std::vector<processor_node_id> ret;
        ret.reserve(nds.size());
        std::transform(nds.begin(), nds.end(), std::back_inserter(ret),
                       [](auto const &n) { return n.id; });
        return ret;
    }

    /**
     * \brief Return all of the edges of this graph.
     *
     * Each edge is a pair (source, destination).
     *
     * \return edges, sorted in ascending order
     */
    [[nodiscard]] auto edges() const
        -> std::vector<std::pair<processor_node_id, processor_node_id>> {
        return edgs;
    }

    /**
     * \brief Return all of the entry points of this graph.
     *
     * \return entry point node ids, sorted in ascending order.
     */
    [[nodiscard]] auto entry_points() const -> std::vector<processor_node_id> {
        return entrypts;
    }

    /**
     * \brief Return whether the given node is an entry point of this graph.
     *
     * \param id the node id
     *
     * \return true if \c id is an entry point of this graph
     */
    [[nodiscard]] auto is_entry_point(processor_node_id id) const -> bool {
        return std::find(entrypts.begin(), entrypts.end(), id) !=
               entrypts.end();
    }

    /**
     * \brief Return the numerical index of the given node in this graph.
     *
     * The returned index maps bijectionally to the node id within the context
     * of this graph, so long as the graph is not modified.
     *
     * \param id a node id in this graph
     *
     * \return integer index of the node
     */
    [[nodiscard]] auto node_index(processor_node_id id) const -> std::size_t {
        auto it = std::find_if(nds.begin(), nds.end(),
                               [id](auto const &n) { return n.id == id; });
        if (it == nds.end())
            throw std::logic_error("no such node id in processor_graph");
        return std::size_t(std::distance(nds.begin(), it));
    }

    /**
     * \brief Return metadata for the processor represented by the given node.
     *
     * \param id a node in this graph
     *
     * \return processor metadata
     */
    [[nodiscard]] auto node_info(processor_node_id id) const
        -> processor_info {
        auto it = std::find_if(nds.begin(), nds.end(),
                               [id](auto const &n) { return n.id == id; });
        if (it == nds.end())
            throw std::logic_error("no such node id in processor_graph");
        return it->info;
    }

    friend auto merge_processor_graphs(processor_graph const &a,
                                       processor_graph const &b)
        -> processor_graph;
};

/**
 * \brief Create a new processor graph by merging two existing ones.
 *
 * \ingroup introspect
 *
 * \param a the first processor graph to merge
 *
 * \param b the second processor graph to merge
 *
 * \return the merged processor graph
 */
[[nodiscard]] inline auto merge_processor_graphs(processor_graph const &a,
                                                 processor_graph const &b)
    -> processor_graph {
    processor_graph c;

    c.nds.reserve(a.nds.size() + b.nds.size());
    std::set_union(a.nds.begin(), a.nds.end(), b.nds.begin(), b.nds.end(),
                   std::back_inserter(c.nds),
                   [](auto const &l, auto const &r) { return l.id < r.id; });

    c.edgs.reserve(a.edgs.size() + b.edgs.size());
    std::set_union(a.edgs.begin(), a.edgs.end(), b.edgs.begin(), b.edgs.end(),
                   std::back_inserter(c.edgs));

    c.entrypts.reserve(a.entrypts.size() + b.entrypts.size());
    std::set_union(a.entrypts.begin(), a.entrypts.end(), b.entrypts.begin(),
                   b.entrypts.end(), std::back_inserter(c.entrypts));

    return c;
}

namespace internal {

inline auto format_hex_addr(std::size_t p) -> std::string {
    std::array<char, sizeof(std::size_t) * 2 + 3> buf{};
    std::fill(buf.begin(), buf.end(), '0');
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto const r =
        std::to_chars(buf.data(), buf.data() + buf.size() - 1, p, 16);
    *r.ptr = '\0';
    std::rotate(buf.begin(),
                std::next(buf.begin(), std::distance(buf.data(), r.ptr) + 1),
                buf.end());
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    buf[1] = 'x';
    return buf.data();
};

} // namespace internal

/**
 * \brief Return a Graphviz dot representation of a processor graph.
 *
 * \ingroup introspect
 *
 * \param graph the processor graph
 *
 * \return Graphviz dot input representing the graph
 */
[[nodiscard]] inline auto
graphviz_from_processor_graph(processor_graph const &graph) -> std::string {
    std::string dot;
    dot += "digraph G {\n";
    for (auto const &node : graph.nodes()) {
        processor_info const info = graph.node_info(node);
        dot += "    n";
        dot += std::to_string(graph.node_index(node));
        dot += " [shape=box label=\"";
        dot += info.name();
        dot += "\" tooltip=\"";
        dot += info.type_name();
        dot += " at ";
        dot += internal::format_hex_addr(info.address());
        dot += "\"];\n";
    }
    for (auto const &edge : graph.edges()) {
        dot += "    n";
        dot += std::to_string(graph.node_index(edge.first));
        dot += " -> n";
        dot += std::to_string(graph.node_index(edge.second));
        dot += ";\n";
    }
    dot += "}\n";
    return dot;
}

} // namespace tcspc
