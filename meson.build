# This file is part of FLIMEvents
# Copyright 2019-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

project(
    'FLIMEvents',
    'cpp',
    default_options: [
        'cpp_std=c++17',
        'warning_level=3',
    ],
)

thread_dep = dependency('threads')

public_inc = include_directories('include')

subdir('include/FLIMEvents')
subdir('doc')
subdir('test')
subdir('examples')

flimevents_dep = declare_dependency(include_directories: public_inc)
meson.override_dependency('FLIMEvents', flimevents_dep)
