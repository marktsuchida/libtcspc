# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

doxygen = find_program(
    'doxygen',
    required: get_option('docs'),
    disabler: true,
)

doxygen_layout_in = custom_target(
    command: [doxygen, '-l', '@OUTPUT@'],
    output: ['DoxygenLayout.xml.in'],
)

genlayout = find_program('genlayout.py')

doxygen_layout = custom_target(
    command: [genlayout, doxygen_layout_in, '@OUTPUT@'],
    output: ['DoxygenLayout.xml'],
)

doxygen_css = files(
    # Rebuild docs when custom CSS changes. Do not bother listing files from
    # doxygen-awesome-css, which we do not regularly edit.
    'doxygen-awesome-css/custom.css',
)

doxygen_werror = get_option('docs_werror') ? 'FAIL_ON_WARNINGS' : 'NO'

doxyfile = configure_file(
    configuration: {
        'DOXYGEN_INPUT': meson.current_source_dir() / '../include',
        'DOXYGEN_EXCLUDE': meson.current_source_dir() / '../include/tcb',
        'DOXYGEN_OUTPUT': meson.current_build_dir() / 'apidoc',
        'DOXYGEN_LAYOUT': meson.current_build_dir() / 'DoxygenLayout.xml',
        'DOXYGEN_CSS': meson.current_source_dir() / 'doxygen-awesome-css',
        'DOXYGEN_WERROR': doxygen_werror,
    },
    input: 'Doxyfile.in',
    output: 'Doxyfile',
)

doc_install_dir = get_option('datadir') / 'doc/libtcspc'

custom_target(
    'docs',
    command: [doxygen, '-q', doxyfile],
    depends: [doxygen_layout],
    depend_files: [public_cpp_headers, doxygen_css],
    output: ['apidoc'],
    install: true,
    install_dir: doc_install_dir,
)
