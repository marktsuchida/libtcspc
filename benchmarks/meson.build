# This file is part of libtcspc
# Copyright 2019-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

benchmark_read_binary_stream_exe = executable(
    'benchmark_read_binary_stream',
    'benchmark_read_binary_stream.cpp',
    include_directories: [public_inc],
    dependencies: [
        benchmark_dep,
        tcb_span_dep,
    ],
)

benchmark('read_binary_stream benchmark', benchmark_read_binary_stream_exe)
