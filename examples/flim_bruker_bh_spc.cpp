/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/tcspc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

void print_out(char const *str) {
    if (std::fputs(str, stdout) == EOF)
        std::terminate();
}

void print_err(char const *str) {
    if (std::fputs(str, stderr) == EOF)
        std::terminate();
}

void usage() {
    print_err(R"(
Usage: flim_bruker_bh_spc options input_file output_file

Options:
    --channel=CHANNEL  Select channel (default: 0)
    --pixel-time=TIME  Set pixel time in macrotime units (required)
    --width=PIXELS     Set pixels per line (required)
    --height=PIXELS    Set lines per frame (required)
    --sum              If given, output only the total of all frames
    --overwrite        If given, overwrite output file if it exists
    --dump-graph       Do not process input; instead emit the processing graph
                       to standard output in Graphviz dot format

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
)");
}

using abstime_type = tcspc::default_data_types::abstime_type;
using channel_type = tcspc::default_data_types::channel_type;

struct pixel_start_event {
    std::int64_t abstime;
};

struct pixel_stop_event {
    std::int64_t abstime;
};

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
    bool dump_graph = false;
};

template <bool Cumulative>
auto make_histo_proc(settings const &settings,
                     std::shared_ptr<tcspc::context> const &ctx) {
    using namespace tcspc;
    auto bsource = recycling_bucket_source<std::uint16_t>::create();
    auto writer = write_binary_stream(
        binary_file_output_stream(settings.output_filename,
                                  arg::truncate{settings.truncate}),
        recycling_bucket_source<std::byte>::create(),
        arg::granularity<>{65536});
    struct reset_event {};
    if constexpr (Cumulative) {
        return append(
            reset_event{}, // Reset before flush to get concluding array.
            scan_histograms<histogram_policy::emit_concluding_events,
                            reset_event>(
                arg::num_elements{settings.pixels_per_line *
                                  settings.lines_per_frame},
                arg::num_bins<>{256}, arg::max_per_bin<u16>{65535}, bsource,
                count<histogram_array_event<>>(
                    ctx->tracker<count_access>("frame_counter"),
                    select<type_list<concluding_histogram_array_event<>>>(
                        extract_bucket<concluding_histogram_array_event<>>(
                            view_as_bytes(std::move(writer)))))));
    } else {
        return scan_histograms<histogram_policy::clear_every_scan>(
            arg::num_elements{settings.pixels_per_line *
                              settings.lines_per_frame},
            arg::num_bins<>{256}, arg::max_per_bin<u16>{65535}, bsource,
            select<type_list<histogram_array_event<>>>(
                count<histogram_array_event<>>(
                    ctx->tracker<count_access>("frame_counter"),
                    extract_bucket<histogram_array_event<>>(
                        view_as_bytes(std::move(writer))))));
    }
}

template <bool Cumulative>
auto make_processor(settings const &settings,
                    std::shared_ptr<tcspc::context> ctx) {
    using namespace tcspc;

    // clang-format off
    return
    read_binary_stream<bh_spc_event>(
        binary_file_input_stream(settings.input_filename,
                                 arg::start_offset<u64>{4}), // 4-byte header.
        arg::max_length{std::numeric_limits<std::uint64_t>::max()},
        recycling_bucket_source<bh_spc_event>::create(),
        arg::granularity<>{65536},
    stop_with_error<type_list<warning_event>>("error reading input",
    unbatch<bucket<bh_spc_event>>(
    count<bh_spc_event>(ctx->tracker<count_access>("record_counter"),
    decode_bh_spc(
    check_monotonic(
    stop_with_error<type_list<warning_event, data_lost_event<>>>(
        "error in input data",
    match<marker_event<>, pixel_start_event>(
        channel_matcher(arg::channel{0}), // Extract pixel clock.
    select_not<type_list<marker_event<>>>(
    generate<pixel_start_event, pixel_stop_event>(
        // Generate pixel stop events.
        one_shot_timing_generator(arg::delay{settings.pixel_time}),
    select_not<type_list<time_reached_event<>>>(
    check_alternating<pixel_start_event, pixel_stop_event>(
    stop_with_error<type_list<warning_event, data_lost_event<>>>(
        "pixel time is such that pixel stop occurs after next pixel start",
    count<pixel_stop_event>(ctx->tracker<count_access>("pixel_counter"),
    route_homogeneous<type_list<time_correlated_detection_event<>>>(
        // Use single-downstream router to select by channel.
        channel_router(
            std::array{std::pair{settings.channel, std::size_t(0)}}),
    map_to_datapoints<time_correlated_detection_event<>>(
        difftime_data_mapper(),
    map_to_bins(power_of_2_bin_mapper<12, 8, true>(),
    cluster_bin_increments<pixel_start_event, pixel_stop_event>(
    make_histo_proc<Cumulative>(settings, ctx)))))))))))))))))));
    // clang-format on
}

void print_stats(settings const &settings,
                 std::shared_ptr<tcspc::context> const &ctx) {
    auto const pixels_per_frame =
        settings.pixels_per_line * settings.lines_per_frame;
    auto const records =
        ctx->access<tcspc::count_access>("record_counter").count();
    auto const pixels =
        ctx->access<tcspc::count_access>("pixel_counter").count();
    auto const frames =
        ctx->access<tcspc::count_access>("frame_counter").count();
    std::ostringstream stream;
    stream << "records decoded: " << records << '\n';
    stream << "pixels finished: " << pixels << '\n';
    stream << "pixels per frame: " << pixels_per_frame << '\n';
    stream << "frames finished: " << frames << '\n';
    stream << "discarded pixels in incomplete frame: "
           << (pixels - frames * pixels_per_frame) << '\n';
    print_out(stream.str().c_str());
}

template <bool Cumulative> void run_and_print(settings const &settings) {
    auto ctx = tcspc::context::create();
    auto proc = make_processor<Cumulative>(settings, ctx);
    if (settings.dump_graph) {
        auto graph = proc.introspect_graph();
        print_out(tcspc::graphviz_from_processor_graph(graph).c_str());
        return;
    }
    try {
        proc.flush();
    } catch (tcspc::end_of_processing const &exc) {
        print_err(exc.what());
        print_err("\n");
    }
    print_stats(settings, ctx);
}

template <typename GetValue>
void parse_option(settings &dest, std::string const &key, GetValue get_value) {
    try {
        if (key == "channel")
            dest.channel = std::stoi(get_value());
        else if (key == "pixel-time")
            dest.pixel_time = std::stoi(get_value());
        else if (key == "width")
            dest.pixels_per_line = std::stoul(get_value());
        else if (key == "height")
            dest.lines_per_frame = std::stoul(get_value());
        else if (key == "sum")
            dest.cumulative = true;
        else if (key == "overwrite")
            dest.truncate = true;
        else if (key == "dump-graph")
            dest.dump_graph = true;
        else
            throw std::invalid_argument("unrecognized option");
    } catch (std::exception const &exc) {
        throw std::invalid_argument("--" + key + ": " + exc.what());
    }
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
            parse_option(ret, key, [&] {
                return equals == std::string::npos ? pop_arg()
                                                   : arg.substr(equals + 1);
            });
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
        print_err(exc.what());
        print_err("\n");
        if (dynamic_cast<std::invalid_argument const *>(&exc) != nullptr)
            usage();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
