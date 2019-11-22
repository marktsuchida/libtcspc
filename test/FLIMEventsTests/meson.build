flimevents_tests_srcs = [
    'BHDeviceEventTests.cpp',
    'FLIMEventsTests.cpp',
    'LineClockPixellatorTests.cpp',
]

flimevents_tests_exe = executable('FLIMEventsTests',
        flimevents_tests_srcs,
        include_directories: [public_inc, catch2_inc],
        )

test('FLIMEvents Tests', flimevents_tests_exe)
