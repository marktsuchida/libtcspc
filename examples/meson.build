# This file is part of libtcspc
# Copyright 2019-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

if not get_option('examples').disabled()

summarize_swabian = executable(
    'summarize_swabian',
    'summarize_swabian.cpp',
    dependencies: [
        libtcspc_dep,
        thread_dep,
    ],
)

endif
