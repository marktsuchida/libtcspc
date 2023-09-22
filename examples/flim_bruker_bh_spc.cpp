/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/bh_spc.hpp"
#include "libtcspc/binning.hpp"
#include "libtcspc/buffer.hpp"
#include "libtcspc/check.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/count.hpp"
#include "libtcspc/generate.hpp"
#include "libtcspc/histogram_elementwise.hpp"
#include "libtcspc/match.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/read_binary_stream.hpp"
#include "libtcspc/route.hpp"
#include "libtcspc/select.hpp"
#include "libtcspc/stop.hpp"
#include "libtcspc/view_as_bytes.hpp"
#include "libtcspc/write_binary_stream.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

void usage() {
    std::fputs(R"(
Usage: flim_bruker_bh_spc options input_file output_file

Options:
    --channel=CHANNEL  Select channel (default: 0)
    --pixel-time=TIME  Set pixel time in macrotime units (required)
    --width=PIXELS     Set pixels per line (required)
    --height=PIXELS    Set lines per frame (required)
    --sum              If given, output only the total of all frames
    --overwrite        If given, overwrite output file if it exists

This program computes FLIM histograms from raw Becker-Hickl SPC files in which
marker 0 is a valid pixel clock (start of each pixel). There must not be any
marker 0 events that are not pixel starts.

The output is a raw binary array file of 16-bit unsigned integers. It can be
read, for example, with numpy.fromfile(output_file, dtype=numpy.uint16).

When --sum is not given, the array has the shape (in NumPy axis order)
    (frame_count, height, width, 256),
where the last axis is the time difference histogram (reduced to 8 bits).

When --sum is given, the array has the shape (height, width, 256).

In all cases, if there is an incomplete frame at the end of the input, it is
excluded from the output.
)",
               stderr);
}

using abstime_type = tcspc::default_data_traits::abstime_type;
using channel_type = tcspc::default_data_traits::channel_type;
struct pixel_start_event : tcspc::base_time_tagged_event<> {};
struct pixel_stop_event : tcspc::base_time_tagged_event<> {};

// Workaround for https://github.com/llvm/llvm-project/issues/54668 (probably
// fixed in LLVM 18):
// NOLINTNEXTLINE(bugprone-exception-escape)
struct settings {
    std::string input_filename;
    std::string output_filename;
    channel_type channel = 0;
    abstime_type pixel_time = -1;
    std::size_t pixels_per_line = 0;
    std::size_t lines_per_frame = 0;
    bool cumulative = false;
    bool truncate = false;
};

template <bool Cumulative>
auto make_histo_proc(settings const &settings,
                     std::shared_ptr<tcspc::processor_context> ctx) {
    using namespace tcspc;
    auto writer = write_binary_stream(
        binary_file_output_stream(settings.output_filename, settings.truncate),
        std::make_shared<object_pool<std::vector<std::byte>>>(), 65536);
    if constexpr (Cumulative) {
        return histogram_elementwise_accumulate<never_event, error_on_overflow,
                                                true>(
            settings.pixels_per_line * settings.lines_per_frame, 256, 65535,
            count<histogram_array_event<>>(
                ctx->tracker<count_access>("frame_counter"),
                select<event_set<concluding_histogram_array_event<>>>(
                    view_histogram_array_as_bytes<
                        concluding_histogram_array_event<>>(
                        std::move(writer)))));
    } else {
        return histogram_elementwise<error_on_overflow>(
            settings.pixels_per_line * settings.lines_per_frame, 256, 65535,
            count<histogram_array_event<>>(
                ctx->tracker<count_access>("frame_counter"),
                select<event_set<histogram_array_event<>>>(
                    view_histogram_array_as_bytes<histogram_array_event<>>(
                        std::move(writer)))));
    }
}

template <bool Cumulative>
auto make_processor(settings const &settings,
                    std::shared_ptr<tcspc::processor_context> ctx) {
    using namespace tcspc;
    using device_event_vector = std::vector<bh_spc_event>;

    // clang-format off
    return
    read_binary_stream<bh_spc_event>(
        binary_file_input_stream(settings.input_filename,
                                 4), // Skip 4-byte header.
        std::numeric_limits<std::uint64_t>::max(),
        std::make_shared<object_pool<device_event_vector>>(2, 2),
        65536, // Reader produces shared_ptr of vectors of device events.
    stop_with_error<event_set<warning_event>>("error reading input",
    dereference_pointer<std::shared_ptr<device_event_vector>>(
    unbatch<device_event_vector, bh_spc_event>(
    count<bh_spc_event>(ctx->tracker<count_access>("record_counter"),
    decode_bh_spc(
    check_monotonic(
    stop_with_error<event_set<warning_event, data_lost_event<>>>(
        "error in input data",
    match<marker_event<>, pixel_start_event>(
        channel_matcher(0), // Extract pixel clock.
    select_not<event_set<marker_event<>>>(
    generate<pixel_start_event>(
        one_shot_timing_generator<pixel_stop_event>(
            settings.pixel_time), // Generate pixel stop events.
    select_not<event_set<time_reached_event<>>>(
    check_alternating<pixel_start_event, pixel_stop_event>(
    stop_with_error<event_set<warning_event, data_lost_event<>>>(
        "pixel time is such that pixel stop occurs after next pixel start",
    count<pixel_stop_event>(ctx->tracker<count_access>("pixel_counter"),
    route_homogeneous<event_set<time_correlated_detection_event<>>>(
        channel_router(
            std::array{std::make_pair(settings.channel, std::size_t(0))}),
        std::array{
    map_to_datapoints(difftime_data_mapper(),
    map_to_bins(power_of_2_bin_mapper<12, 8, true>(),
    batch_bin_increments<pixel_start_event, pixel_stop_event>(
    make_histo_proc<Cumulative>(settings, ctx))))}))))))))))))))));
    // clang-format on
}

void print_stats(settings const &settings,
                 std::shared_ptr<tcspc::processor_context> ctx) {
    auto const pixels_per_frame =
        settings.pixels_per_line * settings.lines_per_frame;
    auto const records =
        ctx->accessor<tcspc::count_access>("record_counter").count();
    auto const pixels =
        ctx->accessor<tcspc::count_access>("pixel_counter").count();
    auto const frames =
        ctx->accessor<tcspc::count_access>("frame_counter").count();
    std::ostringstream stream;
    stream << "records decoded: " << records << '\n';
    stream << "pixels finished: " << pixels << '\n';
    stream << "pixels per frame: " << pixels_per_frame << '\n';
    stream << "frames finished: " << frames << '\n';
    stream << "discarded pixels in incomplete frame: "
           << (pixels - frames * pixels_per_frame) << '\n';
    std::fputs(stream.str().c_str(), stdout);
}

template <bool Cumulative> auto run_and_print(settings const &settings) {
    auto ctx = std::make_shared<tcspc::processor_context>();
    auto proc = make_processor<Cumulative>(settings, ctx);
    try {
        proc.pump_events();
    } catch (tcspc::end_processing const &exc) {
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
    }
    print_stats(settings, ctx);
}

auto parse_args(std::vector<std::string> args) -> settings {
    std::vector<std::string> positional;
    settings ret{};
    auto pop_arg = [&] {
        if (args.empty())
            throw std::invalid_argument("option value expected");
        auto ret = args.front();
        args.erase(args.begin());
        return ret;
    };
    while (not args.empty()) {
        auto const arg = pop_arg();
        if (arg.substr(0, 2) == "--") {
            auto const equals = arg.find('=', 2);
            auto const key = equals == std::string::npos
                                 ? arg.substr(2)
                                 : arg.substr(2, equals - 2);
            auto get_value = [&] {
                return equals == std::string::npos ? pop_arg()
                                                   : arg.substr(equals + 1);
            };
            if (key == "channel")
                ret.channel = std::stoi(get_value());
            else if (key == "pixel-time")
                ret.pixel_time = std::stoi(get_value());
            else if (key == "width")
                ret.pixels_per_line = std::stoul(get_value());
            else if (key == "height")
                ret.lines_per_frame = std::stoul(get_value());
            else if (key == "sum")
                ret.cumulative = true;
            else if (key == "overwrite")
                ret.truncate = true;
            else
                throw std::invalid_argument("unrecognized option: " + arg);
        } else {
            positional.push_back(arg);
        }
    }
    if (ret.pixel_time <= 0) {
        throw std::invalid_argument(
            "--pixel-time must be given and be positive");
    }
    if (ret.pixels_per_line == 0 || ret.lines_per_frame == 0) {
        throw std::invalid_argument(
            "--width and --height must both be given and be positive");
    }
    if (positional.size() != 2) {
        throw std::invalid_argument(
            "two positional arguments required (input file and output file)");
    }
    ret.input_filename = positional[0];
    ret.output_filename = positional[1];
    return ret;
}

auto main(int argc, char *argv[]) -> int {
    try {
        std::vector<std::string> const args(std::next(argv),
                                            std::next(argv, argc));
        auto const settings = parse_args(args);
        if (settings.cumulative)
            run_and_print<true>(settings);
        else
            run_and_print<false>(settings);
    } catch (std::exception const &exc) {
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
        if (dynamic_cast<std::invalid_argument const *>(&exc) != nullptr)
            usage();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
