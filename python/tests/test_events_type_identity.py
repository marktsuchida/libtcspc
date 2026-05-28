# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

"""Safety net for the structural `_type_identity` comparison.

`EventType._type_identity()` lets type equality be decided in pure Python (no
compile) for the common cases. A definite prediction is only sound if every
declared non-alias constructor really is a non-alias class (template). This
test builds representative instances of every concrete `EventType` subclass,
collects every pair whose equality the structural comparator decides
*definitely* (no `None`), and verifies all those predictions against the C++
compiler with `static_assert` in a *single* compile. A misdeclared constructor
(an alias mistaken for a class template) makes a `static_assert` fire and the
build fail, turning a silent false negative into a loud test failure.
"""

import itertools
from collections.abc import Callable

import numpy as np
from libtcspc import _events as E
from libtcspc._cpp_utils import _run_cpp_prog, _same_type_structural
from libtcspc._events import _event_definitions_referenced_by
from libtcspc._numeric_traits import (
    NumericTraits,
    _struct_definitions_referenced_by,
)


def _representative_events() -> list[E.EventType]:
    nt = NumericTraits()
    nt2 = NumericTraits(abstime_type=np.uint64)

    # Every concrete `EventType` subclass parameterised by `NumericTraits`,
    # instantiated with both the default and a non-default traits set so that
    # the comparator must distinguish them.
    templated: list[Callable[[NumericTraits], E.EventType]] = [
        E.BeginLostIntervalEvent,
        E.BinIncrementClusterEvent,
        E.BinIncrementEvent,
        E.BulkCountsEvent,
        E.ConcludingHistogramArrayEvent,
        E.ConcludingHistogramEvent,
        E.DataLostEvent,
        E.DatapointEvent,
        E.DetectionEvent,
        E.DetectionPairEvent,
        E.EndLostIntervalEvent,
        E.HistogramArrayEvent,
        E.HistogramArrayProgressEvent,
        E.HistogramEvent,
        E.LostCountsEvent,
        E.MarkerEvent,
        E.PeriodicSequenceModelEvent,
        E.RealLinearTimingEvent,
        E.RealOneShotTimingEvent,
        E.TimeCorrelatedDetectionEvent,
        E.TimeReachedEvent,
    ]

    # Non-templated leaf events.
    leaf: list[Callable[[], E.EventType]] = [
        E.BHSPC600_256chEvent,
        E.BHSPC600_4096chEvent,
        E.BHSPCEvent,
        E.PQT2GenericEvent,
        E.PQT2HydraHarpV1Event,
        E.PQT2PicoHarp300Event,
        E.PQT3GenericEvent,
        E.PQT3HydraHarpV1Event,
        E.PQT3PicoHarp300Event,
        E.SwabianTagEvent,
        E.WarningEvent,
    ]

    events: list[E.EventType] = []
    for cls in templated:
        events.append(cls(nt))
        events.append(cls(nt2))
    for leaf_cls in leaf:
        events.append(leaf_cls())
    events += [
        E.CustomEvent("ce_idtest_empty"),
        E.CustomEvent("ce_idtest_other"),
        E.CustomEvent("ce_idtest_abst", abstime=True, traits=nt),
        E.CustomEvent("ce_idtest_abst", abstime=True, traits=nt2),
        E._ByteEvent(),
        E.BucketEvent(E._ByteEvent()),
        E.BucketEvent(E.DetectionEvent(nt)),
        E.BucketEvent(E.DetectionEvent(nt2)),
        E.ConstBucketEvent(E._ByteEvent()),
        E.ConstBucketEvent(E.DetectionEvent(nt)),
        E.ConstBucketEvent(E.DetectionEvent(nt2)),
        # Opaque element types (numeric-traits members); these exercise the
        # deferred (compile) path and only yield a definite prediction against
        # an identically spelled counterpart.
        E.BucketEvent(E._TraitsMemberEvent(nt, "bin_type")),
        E.BucketEvent(E._TraitsMemberEvent(nt2, "bin_type")),
        E._TraitsMemberEvent(nt, "bin_type"),
        E._TraitsMemberEvent(nt2, "bin_type"),
        E._TraitsMemberEvent(nt, "bin_index_type"),
    ]
    return events


def test_structural_predictions_match_compiler():
    events = _representative_events()
    names = [e._cpp_type_name() for e in events]
    ids = [e._type_identity() for e in events]

    asserts: list[str] = []
    used_names: set[str] = set()
    for (i, a), (j, b) in itertools.combinations_with_replacement(
        enumerate(ids), 2
    ):
        r = _same_type_structural(a, b)
        if r is None:  # opaque pair: handled by the compiler in normal use
            continue
        predicted = "true" if r else "false"
        asserts.append(
            f"static_assert(std::is_same_v<{names[i]}, {names[j]}> "
            f"== {predicted});"
        )
        used_names.update((names[i], names[j]))

    # The structural comparator must decide some pairs definitely (otherwise the
    # whole optimization is a no-op and this test would be vacuous).
    assert asserts

    event_defs = _event_definitions_referenced_by(used_names)
    trait_defs = _struct_definitions_referenced_by((*used_names, *event_defs))
    defs = trait_defs + event_defs
    prelude = (
        "namespace {\n" + "\n".join(defs) + "\n} // namespace\n"
        if defs
        else ""
    )
    body = "\n".join(asserts)
    code = f"""\
#include "libtcspc/tcspc.hpp"
#include <array>
#include <type_traits>

{prelude}
{body}

int main() {{ return 0; }}
"""
    # A failed static_assert makes the build raise; reaching here means every
    # definite structural prediction agreed with the compiler.
    _run_cpp_prog(code)
