# This file is part of FLIMEvents
# Copyright 2019-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

catch2_dep = dependency(
    'catch2',
    required: get_option('tests'),
    disabler: true
)

flimevents_tests_srcs = [
    'ApplyClassTemplateToTupleElementsTest.cpp',
    'BHDeviceEventsTests.cpp',
    'BroadcastTests.cpp',
    'CommonTests.cpp',
    'CountTests.cpp',
    'DelayHastenProcessorTests.cpp',
    'DiscardTests.cpp',
    'DynamicPolymorphismTests.cpp',
    'EventBufferTests.cpp',
    'EventSetTests.cpp',
    'GateTests.cpp',
    'GenerateTimingsTests.cpp',
    'HistogramTests.cpp',
    'LineClockPixellatorTests.cpp',
    'MergeTests.cpp',
    'PQT3DeviceEventsTests.cpp',
    'PixelPhotonEventsTests.cpp',
    'ReadBytesTests.cpp',
    'RouteByChannelTests.cpp',
    'SplitEventsTests.cpp',
    'SwabianTagEventTests.cpp',
    'TimeDelayTests.cpp',
    'TimeTaggedEventsTests.cpp',
    'TranslateMarkerTests.cpp',
    'main.cpp',
]

flimevents_tests_exe = executable(
    'FLIMEventsTests',
    flimevents_tests_srcs,
    include_directories: [public_inc],
    dependencies: [catch2_dep],
)

test('FLIMEvents Tests', flimevents_tests_exe)
