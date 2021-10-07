flimevents_tests_srcs = [
    'BHDeviceEventsTests.cpp',
    'BroadcastProcessorTests.cpp',
    'CommonTests.cpp',
    'DelayHastenEventsTests.cpp',
    'DynamicPolymorphismTests.cpp',
    'EventBufferTests.cpp',
    'EventSetTests.cpp',
    'HistogramTests.cpp',
    'LineClockPixellatorTests.cpp',
    'PixelPhotonEventsTests.cpp',
    'PixelPhotonRouterTests.cpp',
    'PQT3DeviceEventsTests.cpp',
    'TCSPCEventsTests.cpp',
    'main.cpp',
]

flimevents_tests_exe = executable(
    'FLIMEventsTests',
    flimevents_tests_srcs,
    include_directories: [public_inc],
    dependencies: [catch2_dep],
)

test('FLIMEvents Tests', flimevents_tests_exe)
