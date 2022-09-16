# This file is part of FLIMEvents
# Copyright 2019-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

catch2_dep = dependency(
    'catch2',
    required: get_option('tests'),
    disabler: true
)

flimevents_tests_srcs = [
    'apply_class_template.cpp',
    'bh_spc.cpp',
    'binning.cpp',
    'broadcast.cpp',
    'buffer.cpp',
    'common.cpp',
    'count.cpp',
    'delay_hasten.cpp',
    'discard.cpp',
    'dynamic_polymorphism.cpp',
    'event_set.cpp',
    'flimevt.cpp',
    'gate.cpp',
    'generate_timings.cpp',
    'histogram.cpp',
    'histogram_array.cpp',
    'histogram_events.cpp',
    'legacy_histogram.cpp',
    'line_clock_pixellator.cpp',
    'merge.cpp',
    'picoquant_t3.cpp',
    'pixel_photon_events.cpp',
    'read_bytes.cpp',
    'ref_processor.cpp',
    'route_by_channel.cpp',
    'split.cpp',
    'swabian_tag.cpp',
    'test_utils.cpp',
    'time_delay.cpp',
    'time_tagged_events.cpp',
    'translate_marker.cpp',
]

flimevents_tests_exe = executable(
    'FLIMEventsTests',
    flimevents_tests_srcs,
    include_directories: [public_inc],
    dependencies: [catch2_dep],
)

test('FLIMEvents Tests', flimevents_tests_exe)
