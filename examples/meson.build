# This file is part of libtcspc
# Copyright 2019-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

if not get_option('examples').disabled()

summarize_bh_spc = executable(
    'summarize_bh_spc',
    'summarize_bh_spc.cpp',
    dependencies: [
        libtcspc_dep,
        thread_dep,
    ],
)

summarize_swabian = executable(
    'summarize_swabian',
    'summarize_swabian.cpp',
    dependencies: [
        libtcspc_dep,
        thread_dep,
    ],
)

flim_bruker_bh_spc = executable(
    'flim_bruker_bh_spc',
    'flim_bruker_bh_spc.cpp',
    dependencies: [
        libtcspc_dep,
        thread_dep,
    ],
)

flim_bruker_swabian = executable(
    'flim_bruker_swabian',
    'flim_bruker_swabian.cpp',
    dependencies: [
        libtcspc_dep,
        thread_dep,
    ],
)

endif
