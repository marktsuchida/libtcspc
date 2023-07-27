# This file is part of libtcspc
# Copyright 2019-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

benchmark_read_binary_stream_devzero_exe = executable(
    'read_binary_stream_devzero',
    'read_binary_stream_devzero.cpp',
    include_directories: [public_inc],
    dependencies: [
        benchmark_dep,
        tcb_span_dep,
    ],
)

benchmark(
    'read_binary_stream_devzero',
    benchmark_read_binary_stream_devzero_exe,
)

benchmark_write_binary_stream_devnull_exe = executable(
    'write_binary_stream_devnull',
    'write_binary_stream_devnull.cpp',
    include_directories: [public_inc],
    dependencies: [
        benchmark_dep,
        tcb_span_dep,
    ],
)

benchmark(
    'write_binary_stream_devnull',
    benchmark_write_binary_stream_devnull_exe,
)
