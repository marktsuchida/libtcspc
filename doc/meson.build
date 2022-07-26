doxygen = find_program(
    'doxygen',
    required: get_option('docs'),
)

doxyfile = configure_file(
    configuration: {
        'DOXYGEN_INPUT': meson.current_source_dir() / '../include',
        'DOXYGEN_OUTPUT': meson.current_build_dir() / 'apidoc',
    },
    input: 'Doxyfile.in',
    output: 'Doxyfile',
)

doc_install_dir = get_option('datadir') / 'doc/FLIMEvents'

if doxygen.found()
    custom_target(
        'docs',
        command: [doxygen, doxyfile],
        depend_files: [public_cpp_headers],
        output: ['apidoc'],
        install: true,
        install_dir: doc_install_dir,
    )
endif
