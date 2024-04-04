# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

import pytest
from libtcspc._context import ProcessorContext
from libtcspc._events import EventType
from libtcspc._graph import Graph
from libtcspc._processors import Count, NullSink


def test_context_empty_graph_rejected():
    g = Graph()
    with pytest.raises(ValueError):
        ProcessorContext(g)


def test_context_graph_with_two_inputs_rejected():
    g = Graph()
    g.add_node("a", NullSink())
    g.add_node("b", NullSink())
    with pytest.raises(ValueError):
        ProcessorContext(g)


def test_context_graph_with_output_rejected():
    g = Graph()
    g.add_node("a", Count(EventType("int")))
    with pytest.raises(ValueError):
        ProcessorContext(g)


def test_context_graph_with_single_input_allowed():
    g = Graph()
    g.add_node("a", NullSink())
    c = ProcessorContext(g)
    c.flush()

    g.add_node("c", Count(EventType("int")), downstream="a")
    c = ProcessorContext(g)
    c.flush()
