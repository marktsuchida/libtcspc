# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT


import pytest
from _test_helpers import _NamedEvent, _TestNode
from libtcspc._codegen import _CodeGenerationContext
from libtcspc._cpp_utils import (
    _CppExpression,
    _CppIdentifier,
    _CppTypeName,
    _run_cpp_prog,
)
from libtcspc._graph import Graph, Node

ShortEvent = _NamedEvent(_CppTypeName("short"))
IntEvent = _NamedEvent(_CppTypeName("int"))
LongEvent = _NamedEvent(_CppTypeName("long"))

gencontext = _CodeGenerationContext(
    _CppIdentifier("ctx"), _CppIdentifier("params"), _CppIdentifier("sinks")
)


def test_empty_graph():
    g = Graph()
    assert g._inputs() == ()
    assert g.inputs() == ()
    assert g._outputs() == ()
    assert g.outputs() == ()

    assert g._map_event_sets([]) == ()

    with pytest.raises(ValueError):
        g._map_event_sets([(IntEvent,)])

    code = g._cpp_expression(gencontext)
    # An empty graph has no inputs, so generates a lambda that returns an empty
    # tuple. Assignment should succeed.
    assert (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <tuple>
            int main() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                std::tuple<> t = {code};
                return 0;
            }}
            """)
        == 0
    )

    with pytest.raises(ValueError):
        g._cpp_expression(gencontext, [_CppExpression("downstream")])


def test_single_node(mocker):
    g = Graph()
    node = _TestNode()
    assert g.add_node("node", node) == "node"
    assert g._inputs() == ((0, 0),)
    assert g.inputs() == (("node", "input"),)
    assert g._outputs() == ((0, 0),)
    assert g.outputs() == (("node", "output"),)

    with pytest.raises(ValueError):
        g._map_event_sets([])

    node._map_event_sets = mocker.MagicMock(return_value=((LongEvent,),))  # type: ignore
    assert g._map_event_sets([(IntEvent,)]) == ((LongEvent,),)
    node._map_event_sets.assert_called_with([(IntEvent,)])  # type: ignore

    with pytest.raises(ValueError):
        g._cpp_expression(gencontext)

    def node_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return downstreams[0]

    node._cpp_expression = mocker.MagicMock(side_effect=node_codegen)  # type: ignore
    code = g._cpp_expression(
        gencontext, [_CppExpression("std::move(dstream)")]
    )
    # The generated lambda should return a single-element tuple whose element
    # was moved from 'ds'.
    assert (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>
            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int dstream = 42; // Fake the downstream processor with int
                auto proc = {code};
                static_assert(std::is_same_v<decltype(proc), int>);
                return proc;
            }}
            int main() {{ return f(); }}
            """)
        == 42
    )


def test_two_nodes_two_inputs_two_outputs(mocker):
    # in0                               out0 (short)
    #     \                           /
    #      +- node0 --(int)-- node1 -+
    #     /                           \
    # in1                               out1 (long)

    g = Graph()

    node0 = _TestNode(input=["in0", "in1"])
    node1 = _TestNode(output=["out0", "out1"])
    assert g.add_node("n0", node0) == "n0"
    assert g.add_node("n1", node1) == "n1"

    node0._map_event_sets = mocker.MagicMock(return_value=((IntEvent,),))  # type: ignore
    node1._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((ShortEvent,), (LongEvent,))
    )
    g.connect("n0", "n1")
    node0._map_event_sets.assert_called_with([(), ()])  # type: ignore
    node1._map_event_sets.assert_called_with([(IntEvent,)])  # type: ignore

    def node0_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return f"std::tuple{{2 * {downstreams[0]}, 123}}"

    def node1_codegen(gencontext, downstreams):
        assert len(downstreams) == 2
        return f"5 * ({downstreams[0]} + {downstreams[1]})"

    node0._cpp_expression = mocker.MagicMock(side_effect=node0_codegen)  # type: ignore
    node1._cpp_expression = mocker.MagicMock(side_effect=node1_codegen)  # type: ignore
    code = g._cpp_expression(
        gencontext,
        [_CppExpression("std::move(ds0)"), _CppExpression("std::move(ds1)")],
    )
    assert (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <string>
            #include <tuple>
            #include <type_traits>
            #include <utility>

            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int ds0 = 42, ds1 = 43;
                auto [p0, p1] = {code};
                static_assert(std::is_same_v<decltype(p0), int>);
                static_assert(std::is_same_v<decltype(p1), int>);
                // Work around structured binding limitations:
                return std::tuple(p0, p1);
            }}

            int main(int argc, char *argv[]) {{
                auto procs = f();
                auto proc0 = std::get<0>(procs);
                auto proc1 = std::get<1>(procs);
                return proc0 == 2 * (5 * (42 + 43)) && proc1 == 123 ? 0 : 1;
            }}
            """)
        == 0
    )


def test_two_nodes_two_internal_edges(mocker):
    #                    --(long )--
    #                  /             \
    # input -- node0 -+               +- node1 -- output (int)
    #                  \             /
    #                    --(short)--

    g = Graph()

    node0 = _TestNode(output=["out0", "out1"])
    node1 = _TestNode(input=["in0", "in1"])
    assert g.add_node("n0", node0) == "n0"
    assert g.add_node("n1", node1) == "n1"

    node0._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((LongEvent,), (ShortEvent,))
    )
    node1._map_event_sets = mocker.MagicMock(return_value=((IntEvent,),))  # type: ignore
    with pytest.raises(ValueError):
        g.connect("n0", "n1")
    g.connect(("n0", "out0"), ("n1", "in0"))
    g.connect(("n0", "out1"), ("n1", "in1"))
    node0._map_event_sets.assert_called_with([()])  # type: ignore
    node1._map_event_sets.assert_called_with([(LongEvent,), (ShortEvent,)])  # type: ignore

    def node0_codegen(gencontext, downstreams):
        assert len(downstreams) == 2
        return f"{downstreams[0]} + {downstreams[1]}"

    def node1_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return f"std::tuple{{2 * {downstreams[0]}, 123}}"

    node0._cpp_expression = mocker.MagicMock(side_effect=node0_codegen)  # type: ignore
    node1._cpp_expression = mocker.MagicMock(side_effect=node1_codegen)  # type: ignore
    code = g._cpp_expression(gencontext, [_CppExpression("std::move(ds)")])
    assert (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>
            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int ds = 42;
                auto proc = {code};
                static_assert(std::is_same_v<decltype(proc), int>);
                return proc;
            }}
            int main() {{ return f(); }}
            """)
        == 2 * 42 + 123
    )


def make_simple_node(mocker) -> Node:
    def node_codegen(gencontext, downstreams):
        assert len(downstreams) == 1
        return downstreams[0]

    node = _TestNode()
    node._map_event_sets = mocker.MagicMock(return_value=(IntEvent,))  # type: ignore
    node._cpp_expression = mocker.MagicMock(side_effect=node_codegen)  # type: ignore
    return node


def test_add_chain(mocker):
    node0 = make_simple_node(mocker)
    node1 = make_simple_node(mocker)
    node2 = make_simple_node(mocker)
    node3 = make_simple_node(mocker)
    node4 = make_simple_node(mocker)

    g = Graph()
    g.add_chain([node0, node1])
    g.add_chain([node2, node3], upstream="_TestNode-1")
    g.add_chain([node4], downstream=("_TestNode-0", "input"))
    code = g._cpp_expression(gencontext, [_CppExpression("std::move(ds)")])
    assert (
        _run_cpp_prog(f"""\
            #include "libtcspc/tcspc.hpp"
            #include <type_traits>
            auto f() {{
                auto ctx = tcspc::context::create();
                auto params = 0;
                int ds = 42;
                auto proc = {code};
                static_assert(std::is_same_v<decltype(proc), int>);
                return proc;
            }}
            int main() {{ return f(); }}
            """)
        == 42
    )


def test_add_chain_with_cycle(mocker):
    node0 = make_simple_node(mocker)
    node1 = make_simple_node(mocker)
    node2 = make_simple_node(mocker)
    node3 = make_simple_node(mocker)
    node4 = make_simple_node(mocker)

    g = Graph()
    g.add_chain([node0, node1, node2])
    with pytest.raises(ValueError):
        # Would introduce cycle
        g.add_chain(
            [node3, node4], upstream="_TestNode-2", downstream="_TestNode-0"
        )


def test_add_chain_not_single_input_output(mocker):
    g = Graph()
    n0 = _TestNode(input=["in0", "in1"])
    with pytest.raises(ValueError):
        g.add_chain([n0])
    n1 = _TestNode(output=["out0", "out1"])
    with pytest.raises(ValueError):
        g.add_chain([n1])
    # A 2-output node in middle position is also rejected.
    n2 = make_simple_node(mocker)
    n3 = _TestNode(output=["out0", "out1"])
    n4 = make_simple_node(mocker)
    with pytest.raises(ValueError):
        g.add_chain([n2, n3, n4])


def _make_sink_node(mocker) -> Node:
    def sink_codegen(gencontext, downstreams):
        assert len(downstreams) == 0
        return _CppExpression("sink_proc()")

    node = _TestNode(output=[])
    node._map_event_sets = mocker.MagicMock(return_value=())  # type: ignore
    node._cpp_expression = mocker.MagicMock(side_effect=sink_codegen)  # type: ignore
    return node


def test_add_chain_ending_in_sink(mocker):
    g = Graph()
    n0 = make_simple_node(mocker)
    n1 = make_simple_node(mocker)
    sink = _make_sink_node(mocker)
    g.add_chain([("a", n0), ("b", n1), ("sink", sink)])
    # All three nodes added.
    assert [name for name, _ in g._nodes] == ["a", "b", "sink"]
    # Only "a"'s input is open; no outputs are open (sink has none, a/b are
    # connected downstream).
    assert g.inputs() == (("a", "input"),)
    assert g.outputs() == ()


def test_add_chain_single_sink_node(mocker):
    g = Graph()
    sink = _make_sink_node(mocker)
    g.add_chain([("sink", sink)])
    assert [name for name, _ in g._nodes] == ["sink"]
    # Sink's input is open (no upstream provided).
    assert g.inputs() == (("sink", "input"),)
    assert g.outputs() == ()


def test_add_chain_sink_with_upstream(mocker):
    g = Graph()
    src = make_simple_node(mocker)
    sink = _make_sink_node(mocker)
    g.add_node("src", src)
    g.add_chain([("sink", sink)], upstream="src")
    # Both src's output and sink's input are now connected.
    assert g.inputs() == (("src", "input"),)
    assert g.outputs() == ()


def test_add_chain_sink_with_downstream_raises(mocker):
    g = Graph()
    existing = make_simple_node(mocker)
    g.add_node("existing", existing)
    sink = _make_sink_node(mocker)
    with pytest.raises(ValueError):
        g.add_chain([("sink", sink)], downstream="existing")
    # Graph unchanged: sink was not added.
    assert [name for name, _ in g._nodes] == ["existing"]


def test_add_chain_middle_sink_rejected(mocker):
    g = Graph()
    n0 = make_simple_node(mocker)
    sink = _make_sink_node(mocker)
    n2 = make_simple_node(mocker)
    with pytest.raises(ValueError):
        g.add_chain([n0, sink, n2])


def test_add_node_upstream_dict(mocker):
    g = Graph()
    p0 = make_simple_node(mocker)
    p1 = make_simple_node(mocker)
    g.add_node("p0", p0)
    g.add_node("p1", p1)

    node = _TestNode(input=["in0", "in1"])
    node._map_event_sets = mocker.MagicMock(return_value=((IntEvent,),))  # type: ignore
    assert (
        g.add_node(
            "n",
            node,
            upstream={"in0": ("p0", "output"), "in1": "p1"},
        )
        == "n"
    )
    # Both input ports of "n" are connected (no longer open).
    assert ("n", "in0") not in g.inputs()
    assert ("n", "in1") not in g.inputs()
    # Both producers' outputs are now connected.
    assert ("p0", "output") not in g.outputs()
    assert ("p1", "output") not in g.outputs()


def test_add_node_downstream_dict(mocker):
    g = Graph()
    c0 = make_simple_node(mocker)
    c1 = make_simple_node(mocker)
    g.add_node("c0", c0)
    g.add_node("c1", c1)

    node = _TestNode(output=["out0", "out1"])
    node._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((IntEvent,), (IntEvent,))
    )
    assert (
        g.add_node("n", node, downstream={"out0": "c0", "out1": "c1"}) == "n"
    )
    # Both output ports of "n" are connected (no longer open).
    assert ("n", "out0") not in g.outputs()
    assert ("n", "out1") not in g.outputs()
    # Both consumers' inputs are now connected.
    assert ("c0", "input") not in g.inputs()
    assert ("c1", "input") not in g.inputs()


def test_add_node_both_dicts(mocker):
    g = Graph()
    p0 = make_simple_node(mocker)
    p1 = make_simple_node(mocker)
    c0 = make_simple_node(mocker)
    c1 = make_simple_node(mocker)
    for name, n in (("p0", p0), ("p1", p1), ("c0", c0), ("c1", c1)):
        g.add_node(name, n)

    node = _TestNode(input=["in0", "in1"], output=["out0", "out1"])
    node._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((IntEvent,), (IntEvent,))
    )
    g.add_node(
        "n",
        node,
        upstream={"in0": "p0", "in1": "p1"},
        downstream={"out0": "c0", "out1": "c1"},
    )
    assert ("n", "in0") not in g.inputs()
    assert ("n", "in1") not in g.inputs()
    assert ("n", "out0") not in g.outputs()
    assert ("n", "out1") not in g.outputs()
    assert ("p0", "output") not in g.outputs()
    assert ("p1", "output") not in g.outputs()
    assert ("c0", "input") not in g.inputs()
    assert ("c1", "input") not in g.inputs()


def test_add_node_scalar_forms_still_work(mocker):
    g = Graph()
    n0 = make_simple_node(mocker)
    g.add_node("n0", n0)
    n1 = make_simple_node(mocker)
    g.add_node("n1", n1, upstream="n0")
    # n0's output is connected to n1's input via bare-name shorthand.
    assert ("n0", "output") not in g.outputs()
    assert ("n1", "input") not in g.inputs()

    src = _TestNode(output=["out0"])
    src._map_event_sets = mocker.MagicMock(return_value=((IntEvent,),))  # type: ignore
    g.add_node("src", src)
    n2 = make_simple_node(mocker)
    g.add_node("n2", n2, upstream=("src", "out0"))
    assert ("src", "out0") not in g.outputs()
    assert ("n2", "input") not in g.inputs()


def test_add_node_atomic_rollback(mocker):
    g = Graph()
    c0 = make_simple_node(mocker)
    g.add_node("c0", c0)

    node = _TestNode(output=["out0", "out1"])
    node._map_event_sets = mocker.MagicMock(  # type: ignore
        return_value=((IntEvent,), (IntEvent,))
    )
    with pytest.raises(ValueError):
        # The second connection targets "c0"'s input, already consumed by the
        # first, so it fails after the first edge was made.
        g.add_node("n", node, downstream={"out0": "c0", "out1": "c0"})

    # The node was not added.
    with pytest.raises(KeyError):
        g.node("n")
    assert "n" not in [name for name, _ in g._nodes]
    # The partially-made connection was rolled back: c0's input is free again.
    assert ("c0", "input") in g.inputs()


def test_empty_add_chain_connects_upstream_downstream(mocker):
    g = Graph()
    n0 = _TestNode()
    n1 = _TestNode()
    g.add_node("n0", n0)
    g.add_node("n1", n1)
    n0._map_event_sets = mocker.MagicMock(return_value=((IntEvent,),))  # type: ignore
    n1._map_event_sets = mocker.MagicMock(return_value=((LongEvent,),))  # type: ignore
    g.add_chain([], upstream="n0", downstream="n1")
    n0._map_event_sets.assert_called_with([()])  # type: ignore
    n1._map_event_sets.assert_called_with([(IntEvent,)])  # type: ignore
    with pytest.raises(ValueError):
        # Would introduce cycle
        g.add_chain([], upstream="n1", downstream="n0")
