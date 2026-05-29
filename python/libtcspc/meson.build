# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
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
    '_acquisition_readers.py',
    '_bin_mappers.py',
    '_bucket_sources.py',
    '_codegen.py',
    '_compile.py',
    '_cpp_utils.py',
    '_data_mappers.py',
    '_events.py',
    '_execute.py',
    '_graph.py',
    '_graphviz.py',
    '_include.py',
    '_matchers.py',
    '_node.py',
    '_numeric_traits.py',
    '_param.py',
    '_processors/__init__.py',
    '_processors/_acquisition.py',
    '_processors/_batching.py',
    '_processors/_binning.py',
    '_processors/_branching.py',
    '_processors/_buffering.py',
    '_processors/_common.py',
    '_processors/_core.py',
    '_processors/_decoding.py',
    '_processors/_filtering.py',
    '_processors/_histogramming.py',
    '_processors/_input_output.py',
    '_processors/_merging.py',
    '_processors/_mux.py',
    '_processors/_pairing.py',
    '_processors/_statistics.py',
    '_processors/_stopping.py',
    '_processors/_testing.py',
    '_processors/_time_correlation.py',
    '_processors/_timeline.py',
    '_processors/_timing.py',
    '_processors/_timing_modeling.py',
    '_processors/_validation.py',
    '_routers.py',
    '_streams.py',
    '_timing_generators.py',
    'py.typed',
    '_odext/__init__.py',
    '_odext/_async.py',
)

python.install_sources(
    py_sources,
    preserve_path: true,
    subdir: py_package_name,
)

# It is not well defined where wheel "data" is installed, so the recommended
# practice is to install "data" files together with Python source.
python.install_sources(
    public_cpp_headers,
    preserve_path: true,
    subdir: py_package_name / 'include',
)
