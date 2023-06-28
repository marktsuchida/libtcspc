# This file is part of FLIMEvents
# Copyright 2019-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

project(
    'FLIMEvents',
    'cpp',
    default_options: [
        'cpp_std=c++17',
        'warning_level=3',
        'cpp_arg=-Wconversion',
    ],
)

thread_dep = dependency('threads')

tcb_span_dep = dependency('tcb_span')

catch2_with_main_dep = dependency(
    'catch2-with-main',
    required: get_option('tests'),
    disabler: true,
)

public_inc = include_directories('include')

subdir('include/libtcspc')
subdir('doc')
subdir('test')
subdir('examples')

flimevents_dep = declare_dependency(
    include_directories: public_inc,
    dependencies: [
        tcb_span_dep,
    ],
)
meson.override_dependency('FLIMEvents', flimevents_dep)
