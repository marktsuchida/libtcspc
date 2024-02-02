# This file is part of libtcspc
# Copyright 2019-2023 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

doxygen = find_program(
    'doxygen',
    required: get_option('docs'),
    disabler: true,
)

doxyfile = configure_file(
    configuration: {
        'DOXYGEN_INPUT': meson.current_source_dir() / '../include',
        'DOXYGEN_EXCLUDE': meson.current_source_dir() / '../include/tcb',
        'DOXYGEN_OUTPUT': meson.current_build_dir() / 'apidoc',
    },
    input: 'Doxyfile.in',
    output: 'Doxyfile',
)

doc_install_dir = get_option('datadir') / 'doc/libtcspc'

custom_target(
    'docs',
    command: [doxygen, '-q', doxyfile],
    depend_files: [public_cpp_headers],
    output: ['apidoc'],
    install: true,
    install_dir: doc_install_dir,
)
