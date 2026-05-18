# This file is part of libtcspc
# Copyright 2019-2026 Board of Regents of the University of Wisconsin System
# SPDX-License-Identifier: MIT

sphinx_build = find_program(
    'sphinx-build',
    required: get_option('python_docs'),
    disabler: true,
)

py_doc_sources = files(
    'conf.py',
    '_static/custom.css',
    '_templates/autosummary/class.rst',
    'index.md',
    'api/index.md',
    'api/graph_definition.md',
    'api/event_types.md',
    'api/processor_nodes.md',
    'api/auxiliary_objects.md',
    'api/compilation.md',
    'api/execution.md',
    'user_guide/index.md',
    'user_guide/introduction.md',
    'user_guide/installation.md',
    'user_guide/quickstart.md',
    'user_guide/concepts.md',
    'user_guide/building_graphs.md',
    'user_guide/running_graphs.md',
    'user_guide/custom_processors.md',
)

# Listed explicitly (rather than via glob) so additions are intentional. Keep
# in sync with python/libtcspc/meson.build:py_sources (excluding _odext, which
# is internal and undocumented).
python_pkg_sources = files(
    '../../python/libtcspc/__init__.py',
    '../../python/libtcspc/_access.py',
    '../../python/libtcspc/_acquisition_readers.py',
    '../../python/libtcspc/_bucket_sources.py',
    '../../python/libtcspc/_codegen.py',
    '../../python/libtcspc/_compile.py',
    '../../python/libtcspc/_cpp_utils.py',
    '../../python/libtcspc/_events.py',
    '../../python/libtcspc/_execute.py',
    '../../python/libtcspc/_graph.py',
    '../../python/libtcspc/_include.py',
    '../../python/libtcspc/_node.py',
    '../../python/libtcspc/_numeric_traits.py',
    '../../python/libtcspc/_param.py',
    '../../python/libtcspc/_processors.py',
    '../../python/libtcspc/_streams.py',
)
