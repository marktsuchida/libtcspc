# This file is part of libtcspc
# Copyright 2019-2022 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

spctohistogram_srcs = [
    'SPCToHistogram.cpp',
]

spctohistogram_exe = executable(
    'SPCToHistogram',
    spctohistogram_srcs,
    include_directories: public_inc,
    dependencies: [
        tcb_span_dep,
        thread_dep,
    ],
)
