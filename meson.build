# This file is part of libtcspc
# Copyright 2019-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

project(
    'libtcspc',
    'cpp',
    default_options: [
        'cpp_std=c++17',
        'warning_level=3',
        'werror=true',
    ],
)

cxx = meson.get_compiler('cpp')
if cxx.get_id() in ['gcc', 'clang']
    add_project_arguments('-Wconversion', language: 'cpp')
endif

thread_dep = dependency('threads')

tcb_span_dep = dependency('tcb_span')

catch2_with_main_dep = dependency(
    'catch2-with-main',
    allow_fallback: true,
    required: get_option('tests'),
    disabler: true,
    include_type: 'system',
)

trompeloeil_dep = dependency(
    'trompeloeil',
    fallback: ['trompeloeil', 'trompeloeil_dep'],
    required: get_option('tests'),
    disabler: true,
    include_type: 'system',
)

benchmark_dep = dependency(
    'benchmark',
    allow_fallback: true,
    required: get_option('benchmarks'),
    disabler: true,
    include_type: 'system',
)

libtcspc_dep = declare_dependency(
    include_directories: include_directories('include'),
    dependencies: [
        tcb_span_dep,
    ],
)

subdir('include')
subdir('doc')
subdir('test')
subdir('benchmarks')
subdir('examples')
