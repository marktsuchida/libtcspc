catch2_dep = dependency(
    'catch2',
    required: get_option('tests'),
    disabler: true
)

flimevents_tests_srcs = [
    'ApplyClassTemplateToTupleElementsTest.cpp',
    'BHDeviceEventsTests.cpp',
    'BroadcastTests.cpp',
    'CommonTests.cpp',
    'DelayHastenProcessorTests.cpp',
    'DiscardTests.cpp',
    'DynamicPolymorphismTests.cpp',
    'EventBufferTests.cpp',
    'EventSetTests.cpp',
    'HistogramTests.cpp',
    'LineClockPixellatorTests.cpp',
    'MergeTests.cpp',
    'PQT3DeviceEventsTests.cpp',
    'PixelPhotonEventsTests.cpp',
    'RoutePhotonsTests.cpp',
    'SplitEventsTests.cpp',
    'TCSPCEventsTests.cpp',
    'TimeDelayTests.cpp',
    'TranslateMarkerTests.cpp',
    'main.cpp',
]

flimevents_tests_exe = executable(
    'FLIMEventsTests',
    flimevents_tests_srcs,
    include_directories: [public_inc],
    dependencies: [catch2_dep],
)

test('FLIMEvents Tests', flimevents_tests_exe)
