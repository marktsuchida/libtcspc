flimevents_tests_srcs = [
    'BHDeviceEventTests.cpp',
    'BroadcastProcessorTests.cpp',
    'CommonTests.cpp',
    'DecodedEventTests.cpp',
    'DelayHastenEventsTests.cpp',
    'DeviceEventTests.cpp',
    'DynamicPolymorphismTests.cpp',
    'EventBufferTests.cpp',
    'EventSetTests.cpp',
    'HistogramTests.cpp',
    'LineClockPixellatorTests.cpp',
    'PixelPhotonEventTests.cpp',
    'PixelPhotonRouterTests.cpp',
    'PQT3DeviceEventTests.cpp',
    'main.cpp',
]

flimevents_tests_exe = executable(
    'FLIMEventsTests',
    flimevents_tests_srcs,
    include_directories: [public_inc],
    dependencies: [catch2_dep],
)

test('FLIMEvents Tests', flimevents_tests_exe)
