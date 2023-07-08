# This file is part of libtcspc
# Copyright 2019-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

libtcspc_tests_srcs = [
    'apply_class_template.cpp',
    'autocopy_span.cpp',
    'bh_spc.cpp',
    'binning.cpp',
    'broadcast.cpp',
    'buffer.cpp',
    'common.cpp',
    'count.cpp',
    'delay_hasten.cpp',
    'discard.cpp',
    'event_set.cpp',
    'gate.cpp',
    'generate_timings.cpp',
    'histogram.cpp',
    'histogram_elementwise.cpp',
    'histogram_events.cpp',
    'histogramming.cpp',
    'legacy_histogram.cpp',
    'line_clock_pixellator.cpp',
    'match.cpp',
    'merge.cpp',
    'npint.cpp',
    'picoquant_t3.cpp',
    'pixel_photon_events.cpp',
    'read_bytes.cpp',
    'ref_processor.cpp',
    'route.cpp',
    'shared_processor.cpp',
    'split.cpp',
    'swabian_tag.cpp',
    'test_utils.cpp',
    'time_delay.cpp',
    'time_tagged_events.cpp',
    'type_erased_processor.cpp',
    'vector_queue.cpp',
]

libtcspc_tests_exe = executable(
    'test_libtcspc',
    libtcspc_tests_srcs,
    include_directories: [public_inc],
    dependencies: [
        tcb_span_dep,
        catch2_with_main_dep
    ],
)

test('libtcspc tests', libtcspc_tests_exe)
