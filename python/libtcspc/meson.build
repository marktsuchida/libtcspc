# This file is part of libtcspc
# Copyright 2019-2024 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

python = import('python').find_installation(pure: false)

py_package_name = 'libtcspc'

configure_file(
    configuration: {'VERSION': meson.project_version()},
    input: '_version.py.in',
    output: '_version.py',
    install: true,
    install_dir: python.get_install_dir() / py_package_name,
    install_tag: 'python-runtime',
)

py_sources = files(
    '__init__.py',
    '_access.py',
    '_bucket_sources.py',
    '_context.py',
    '_cpp_utils.py',
    '_data_traits.py',
    '_events.py',
    '_graph.py',
    '_processors.py',
    '_streams.py',
    'py.typed',
)

python.install_sources(
    py_sources,
    subdir: py_package_name,
)

# It is not well defined where wheel "data" is installed, so the recommended
# practice is to install "data" files together with Python source.
python.install_sources(
    public_cpp_headers,
    preserve_path: true,
    subdir: py_package_name / 'include',
)
