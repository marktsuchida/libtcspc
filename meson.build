project(
    'FLIMEvents',
    'cpp',
    default_options: [
        'cpp_std=c++17',
    ],
)

thread_dep = dependency('threads')

public_inc = include_directories('include')

subdir('include')
subdir('test')
subdir('examples')

flimevents_dep = declare_dependency(include_directories: public_inc)
meson.override_dependency('FLIMEvents', flimevents_dep)
