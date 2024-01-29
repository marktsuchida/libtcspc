# This file is part of libtcspc
# Copyright 2019-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

benchmark_read_binary_stream_devzero_exe = executable(
    'read_binary_stream_devzero',
    'read_binary_stream_devzero.cpp',
    dependencies: [
        benchmark_dep,
        libtcspc_dep,
    ],
)

benchmark(
    'read_binary_stream_devzero',
    benchmark_read_binary_stream_devzero_exe,
)

benchmark_write_binary_stream_devnull_exe = executable(
    'write_binary_stream_devnull',
    'write_binary_stream_devnull.cpp',
    dependencies: [
        benchmark_dep,
        libtcspc_dep,
    ],
)

benchmark(
    'write_binary_stream_devnull',
    benchmark_write_binary_stream_devnull_exe,
)

benchmark_vector_queue_exe = executable(
    'vector_queue',
    'vector_queue.cpp',
    dependencies: [
        benchmark_dep,
        libtcspc_dep,
    ],
)

benchmark(
    'vector_queue',
    benchmark_vector_queue_exe,
)
