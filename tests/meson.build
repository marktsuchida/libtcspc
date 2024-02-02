# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

test_libtcspc_srcs = [
    'apply_class_template.cpp',
    'autocopy_span.cpp',
    'bh_spc.cpp',
    'binning.cpp',
    'buffer.cpp',
    'check.cpp',
    'common.cpp',
    'count.cpp',
    'delay.cpp',
    'dither.cpp',
    'event_set.cpp',
    'fit_sequence.cpp',
    'gate.cpp',
    'generate.cpp',
    'histogram.cpp',
    'histogram_elementwise.cpp',
    'histogram_events.cpp',
    'histogramming.cpp',
    'introspect.cpp',
    'match.cpp',
    'merge.cpp',
    'multiplex.cpp',
    'npint.cpp',
    'pair.cpp',
    'picoquant_t2.cpp',
    'picoquant_t3.cpp',
    'processor_context.cpp',
    'read_binary_stream.cpp',
    'read_bytes.cpp',
    'recover_order.cpp',
    'regulate_time_reached.cpp',
    'route.cpp',
    'select.cpp',
    'stop.cpp',
    'swabian_tag.cpp',
    'test_utils.cpp',
    'time_correlate.cpp',
    'time_tagged_events.cpp',
    'timing_misc.cpp',
    'type_erased_processor.cpp',
    'vector_queue.cpp',
    'view_as_bytes.cpp',
    'write_binary_stream.cpp',
]

test_libtcspc_exe = executable(
    'test_libtcspc',
    test_libtcspc_srcs,
    dependencies: [
        catch2_with_main_dep,
        libtcspc_dep,
        trompeloeil_dep,
    ],
)

test('libtcspc tests', test_libtcspc_exe)
