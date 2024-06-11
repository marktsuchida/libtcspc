# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

python = import('python').find_installation(pure: false)

test_libtcspc_py_srcs = [
    'handle_span.cpp',
]

test_libtcspc_py_exe = executable(
    'test_libtcspc_py',
    test_libtcspc_py_srcs,
    include_directories: [
        libtcspc_py_include_dir,
    ],
    dependencies: [
        libtcspc_dep,
        python.dependency(embed: true),
    ],
)

test('libtcspc-py tests', test_libtcspc_py_exe)
