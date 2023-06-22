# This file is part of FLIMEvents
# Copyright 2019-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

project(
    'tcb_span',
    'cpp',
)

includes = include_directories('include')

install_headers(
    [
        'include/tcb/span.hpp',
    ],
    subdir: 'tcb',
)

tcb_span_dep = declare_dependency(
    include_directories: includes,
)
