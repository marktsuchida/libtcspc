/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/introspect.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>

namespace tcspc {

namespace internal {

TEST_CASE("Unmangled name is not demangled", "[maybe_demangle]") {
    CHECK(maybe_demangle("not-mangled") == "not-mangled");
}

#ifdef LIBTCSPC_HAVE_CXA_DEMANGLE
TEST_CASE("Mangled name is demangled", "[maybe_demangle]") {
    CHECK(maybe_demangle("_ZN6somens9someclass8somefuncEv") ==
          "somens::someclass::somefunc()");
}
#endif

} // namespace internal

TEST_CASE("processor graph node id", "[processor_node_id]") {
    std::type_index const ti = typeid(int);
    std::type_index const td = typeid(double);
    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    union {
        int i;
        double d;
    } const uu[2]{};
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    auto const &u0 = uu[0];
    auto const &u1 = uu[1];
    using pnid = processor_node_id;

    // All pnid pairs with less/equal/greater elements.
    // Address: u0 < u1
    // Type:    i < d  or  i > d
    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    auto const ee = std::make_pair(pnid(&u0.i), pnid(&u0.i));
    auto const el = ti < td ? std::make_pair(pnid(&u0.i), pnid(&u0.d))
                            : std::make_pair(pnid(&u0.d), pnid(&u0.i));
    auto const eg = ti > td ? std::make_pair(pnid(&u0.i), pnid(&u0.d))
                            : std::make_pair(pnid(&u0.d), pnid(&u0.i));
    auto const le = std::make_pair(pnid(&u0.i), pnid(&u1.i));
    auto const ll = ti < td ? std::make_pair(pnid(&u0.i), pnid(&u1.d))
                            : std::make_pair(pnid(&u0.d), pnid(&u1.i));
    auto const lg = ti > td ? std::make_pair(pnid(&u0.i), pnid(&u1.d))
                            : std::make_pair(pnid(&u0.d), pnid(&u1.i));
    auto const ge = std::make_pair(pnid(&u1.i), pnid(&u0.i));
    auto const gl = ti < td ? std::make_pair(pnid(&u1.i), pnid(&u0.d))
                            : std::make_pair(pnid(&u1.d), pnid(&u0.i));
    auto const gg = ti > td ? std::make_pair(pnid(&u1.i), pnid(&u0.d))
                            : std::make_pair(pnid(&u1.d), pnid(&u0.i));
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    CHECK(ee.first == ee.second);
    CHECK_FALSE(el.first == el.second);
    CHECK_FALSE(eg.first == eg.second);
    CHECK_FALSE(le.first == le.second);
    CHECK_FALSE(ll.first == ll.second);
    CHECK_FALSE(lg.first == lg.second);
    CHECK_FALSE(ge.first == ge.second);
    CHECK_FALSE(gl.first == gl.second);
    CHECK_FALSE(gg.first == gg.second);

    CHECK_FALSE(ee.first != ee.second);
    CHECK(el.first != el.second);
    CHECK(eg.first != eg.second);
    CHECK(le.first != le.second);
    CHECK(ll.first != ll.second);
    CHECK(lg.first != lg.second);
    CHECK(ge.first != ge.second);
    CHECK(gl.first != gl.second);
    CHECK(gg.first != gg.second);

    CHECK_FALSE(ee.first < ee.second);
    CHECK(el.first < el.second);
    CHECK_FALSE(eg.first < eg.second);
    CHECK(le.first < le.second);
    CHECK(ll.first < ll.second);
    CHECK(lg.first < lg.second);
    CHECK_FALSE(ge.first < ge.second);
    CHECK_FALSE(gl.first < gl.second);
    CHECK_FALSE(gg.first < gg.second);

    CHECK_FALSE(ee.first > ee.second);
    CHECK_FALSE(el.first > el.second);
    CHECK(eg.first > eg.second);
    CHECK_FALSE(le.first > le.second);
    CHECK_FALSE(ll.first > ll.second);
    CHECK_FALSE(lg.first > lg.second);
    CHECK(ge.first > ge.second);
    CHECK(gl.first > gl.second);
    CHECK(gg.first > gg.second);

    CHECK(ee.first <= ee.second);
    CHECK(el.first <= el.second);
    CHECK_FALSE(eg.first <= eg.second);
    CHECK(le.first <= le.second);
    CHECK(ll.first <= ll.second);
    CHECK(lg.first <= lg.second);
    CHECK_FALSE(ge.first <= ge.second);
    CHECK_FALSE(gl.first <= gl.second);
    CHECK_FALSE(gg.first <= gg.second);

    CHECK(ee.first >= ee.second);
    CHECK_FALSE(el.first >= el.second);
    CHECK(eg.first >= eg.second);
    CHECK_FALSE(le.first >= le.second);
    CHECK_FALSE(ll.first >= ll.second);
    CHECK_FALSE(lg.first >= lg.second);
    CHECK(ge.first >= ge.second);
    CHECK(gl.first >= gl.second);
    CHECK(gg.first >= gg.second);
}

TEST_CASE("empty processor_graph", "[processor_graph]") {
    int const p{};

    auto const g = processor_graph();
    CHECK(g.nodes().empty());
    CHECK(g.edges().empty());
    CHECK_THROWS_AS(g.node_info(processor_node_id(&p)), std::logic_error);

    auto const m = merge_processor_graphs(g, g);
    CHECK(m.nodes().empty());
    CHECK(m.edges().empty());
    CHECK_THROWS_AS(m.node_info(processor_node_id(&p)), std::logic_error);
}

TEST_CASE("processor_graph push_entry_point", "[processor_graph]") {
    struct test_proc {
        [[nodiscard]] auto introspect_node() const -> processor_info {
            return processor_info(this, "test_proc");
        }
    };
    auto g = processor_graph();

    test_proc const p0;
    g.push_entry_point(&p0);
    CHECK(g.nodes().size() == 1);
    auto const node0 = g.nodes()[0];
    CHECK(g.edges().empty());
    CHECK(g.is_entry_point(node0));
    CHECK(g.node_index(node0) == 0);
    CHECK(g.node_info(node0).name() == "test_proc");

    test_proc const p1;
    g.push_entry_point(&p1);
    CHECK(g.nodes().size() == 2);
    auto const node1 = [&] {
        auto const nodes = g.nodes();
        return nodes[0] == node0 ? nodes[1] : nodes[0];
    }();
    CHECK(g.edges().size() == 1);
    CHECK(g.edges()[0] == std::make_pair(node1, node0));
    CHECK_FALSE(g.is_entry_point(node0));
    CHECK(g.is_entry_point(node1));

    test_proc const p2;
    g.push_source(&p2);
    CHECK(g.nodes().size() == 3);
    auto const node2 = [&] {
        auto const nodes = g.nodes();
        std::set<processor_node_id> nodeset(nodes.begin(), nodes.end());
        nodeset.erase(node0);
        nodeset.erase(node1);
        CHECK(nodeset.size() == 1);
        return *nodeset.begin();
    }();
    CHECK_FALSE(g.is_entry_point(node0));
    CHECK_FALSE(g.is_entry_point(node1));
    CHECK_FALSE(g.is_entry_point(node2));
}

TEST_CASE("processor_graph merge", "[processor_graph]") {
    struct test_proc {
        std::string name;
        [[nodiscard]] auto introspect_node() const -> processor_info {
            return processor_info(this, name);
        }
    };

    auto node_by_name = [](processor_graph const &g, std::string const &name) {
        for (auto const &n : g.nodes()) {
            if (g.node_info(n).name() == name)
                return n;
        }
        throw;
    };

    SECTION("merge with empty") {
        test_proc const p0{"p0"};
        test_proc const p1{"p1"};

        auto g = processor_graph();
        g.push_entry_point(&p0);
        g.push_entry_point(&p1);
        auto h = processor_graph();

        auto const m = merge_processor_graphs(g, h);
        CHECK(m.nodes().size() == 2);
        auto const node0 = node_by_name(m, "p0");
        auto const node1 = node_by_name(m, "p1");
        CHECK(m.edges().size() == 1);
        CHECK(m.edges()[0] == std::pair{node1, node0});
        CHECK_FALSE(m.is_entry_point(node0));
        CHECK(m.is_entry_point(node1));
    }

    SECTION("non-overlapping") {
        test_proc const p0{"p0"};
        test_proc const p1{"p1"};
        test_proc const p2{"p2"};
        test_proc const p3{"p3"};

        auto g = processor_graph();
        g.push_entry_point(&p0);
        g.push_entry_point(&p1);
        auto h = processor_graph();
        h.push_entry_point(&p2);
        h.push_entry_point(&p3);

        auto const m = merge_processor_graphs(g, h);
        CHECK(m.nodes().size() == 4);
        auto const node0 = node_by_name(m, "p0");
        auto const node1 = node_by_name(m, "p1");
        auto const node2 = node_by_name(m, "p2");
        auto const node3 = node_by_name(m, "p3");
        CHECK(m.edges().size() == 2);
        CHECK_FALSE(m.is_entry_point(node0));
        CHECK(m.is_entry_point(node1));
        CHECK_FALSE(m.is_entry_point(node2));
        CHECK(m.is_entry_point(node3));
    }

    SECTION("overlapping") {
        test_proc const p0{"p0"};
        test_proc const p1{"p1"};

        auto g = processor_graph();
        g.push_entry_point(&p0);
        g.push_entry_point(&p1);
        auto h = processor_graph();
        h.push_entry_point(&p0);
        h.push_entry_point(&p1);

        auto const m = merge_processor_graphs(g, h);
        CHECK(m.nodes().size() == 2);
        auto const node0 = node_by_name(m, "p0");
        auto const node1 = node_by_name(m, "p1");
        CHECK(m.edges().size() == 1);
        CHECK(m.edges()[0] == std::pair{node1, node0});
        CHECK_FALSE(m.is_entry_point(node0));
        CHECK(m.is_entry_point(node1));
    }

    SECTION("branching") {
        test_proc const p0{"p0"};
        test_proc const p1{"p1"};
        test_proc const p2{"p2"};
        test_proc const p3{"p3"};

        auto g = processor_graph();
        g.push_entry_point(&p0);
        g.push_entry_point(&p1);
        g.push_entry_point(&p2);
        auto h = processor_graph();
        h.push_entry_point(&p0);
        h.push_entry_point(&p3);
        h.push_entry_point(&p2);

        auto const m = merge_processor_graphs(g, h);
        CHECK(m.nodes().size() == 4);
        auto const node0 = node_by_name(m, "p0");
        auto const node1 = node_by_name(m, "p1");
        auto const node2 = node_by_name(m, "p2");
        auto const node3 = node_by_name(m, "p3");
        CHECK(m.edges().size() == 4);
        CHECK_FALSE(m.is_entry_point(node0));
        CHECK_FALSE(m.is_entry_point(node1));
        CHECK(m.is_entry_point(node2));
        CHECK_FALSE(m.is_entry_point(node3));
    }
}

namespace internal {

TEST_CASE("format hex addr", "[processor_graph]") {
    if constexpr (sizeof(std::size_t) == 8) {
        CHECK(format_hex_addr(0) == "0x0000000000000000");
        CHECK(format_hex_addr(1) == "0x0000000000000001");
        CHECK(format_hex_addr(0x10000000'00000000uLL) == "0x1000000000000000");
    } else if constexpr (sizeof(std::size_t) == 4) {
        CHECK(format_hex_addr(0) == "0x00000000");
        CHECK(format_hex_addr(1) == "0x00000001");
        CHECK(format_hex_addr(0x10000000u) == "0x10000000");
    } else {
        CHECK(false);
    }
}

} // namespace internal

} // namespace tcspc
