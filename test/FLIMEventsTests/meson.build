flimevents_tests_srcs = [
    'BHDeviceEventTests.cpp',
    'FLIMEventsTests.cpp',
    'HistogramTests.cpp',
    'LineClockPixellatorTests.cpp',
]

flimevents_tests_exe = executable(
    'FLIMEventsTests',
    flimevents_tests_srcs,
    include_directories: [public_inc],
    dependencies: [catch2_dep],
)

test('FLIMEvents Tests', flimevents_tests_exe)
