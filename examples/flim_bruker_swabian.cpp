/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/binning.hpp"
#include "libtcspc/buffer.hpp"
#include "libtcspc/check.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/count.hpp"
#include "libtcspc/delay.hpp"
#include "libtcspc/generate.hpp"
#include "libtcspc/histogram_elementwise.hpp"
#include "libtcspc/match.hpp"
#include "libtcspc/merge.hpp"
#include "libtcspc/pair.hpp"
#include "libtcspc/processor_context.hpp"
#include "libtcspc/read_binary_stream.hpp"
#include "libtcspc/recover_order.hpp"
#include "libtcspc/route.hpp"
#include "libtcspc/select.hpp"
#include "libtcspc/stop.hpp"
#include "libtcspc/swabian_tag.hpp"
#include "libtcspc/time_correlate.hpp"
#include "libtcspc/view_as_bytes.hpp"
#include "libtcspc/write_binary_stream.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

void usage() {
    std::fputs(R"(
Usage: flim_bruker_swabian options input_file output_file

Options:
    --sync-channel=CHANNEL
        Specify the channel containing the laser sync signal (required)
    --pixel-marker-channel=CHANNEL
        Specify the channel containing the pixel marker (required)
    --photon-channels=LEADING,TRAILING
        Specify the two channels containing the leading and trailing (often,
        falling and rising) edges of the photon pulses (required)
    --sync-delay=PICOSECONDS
        Specify how much to delay the laser sync signal relative to the other
        signals. Negative values are allowed (and are typical). (default: 0)
    --max-photon-pulse-width=PICOSECONDS
        Consider only photons with at most this much time between leading and
        trailing edges (default: 100000 (= 100 ns))
    --max-diff-time=PICOSECONDS
        Consider only photons within this much time since the previous laser
        sync (default: 15000 (= 15 ns))
    --pixel-time=PICOSECONDS
        Set pixel time (required)
    --width=PIXELS
        Set pixels per line (required)
    --height=PIXELS
        Set lines per frame (required)
    --bin-width=PICOSECONDS
        Set difference time histogram bin width (default: 50)
    --bin-count=COUNT
        Set number of difference time histogram bins (default: 256)
    --sum
        If given, output only the total of all frames
    --overwrite
        If given, overwrite output file if it exists
    --help
        Show this usage and exit

This program computes FLIM histograms from raw Swabian tag dumps (16-byte
binary records; not to be confused with Swabian .ttbin files). In addition to
the rising and falling edges of the photons, the data must contain the laser
sync signal (typically with conditional filter applied by hardware) and a pixel
marker signal indicating the pixel starts.

Photon times are computed as the midpoint between the leading and trailing
edges of the pulse. The photons are then time-correlated with the laser sync
signal, with the laser sync being the start and the photon being the stop of
the difference time measurement.

Usually acquisition should be done with the laser sync signal being
conditionally filtered in hardware, triggered by the photon signal, so it is
necessary to apply a negative delay (--sync-delay) to the laser sync.

The output is a raw binary array file of 16-bit unsigned integers. It can be
read, for example, with numpy.fromfile(output_file, dtype=numpy.uint16).

When --sum is not given, the array has the shape (in NumPy axis order)
    (frame_count, height, width, bin_count).

When --sum is given, the array has the shape (height, width, bin_count).

In all cases, if there is an incomplete frame at the end of the input, it is
excluded from the output.

To work with data produced by Bruker software, processing stops without an
error upon detection of a decreasing timestamp in the input.
)",
               stderr);
}

using abstime_type = tcspc::default_data_traits::abstime_type;
using difftime_type = tcspc::default_data_traits::difftime_type;
using channel_type = tcspc::default_data_traits::channel_type;
using bin_index_type = tcspc::default_data_traits::bin_index_type;
struct pixel_start_event : tcspc::base_time_tagged_event<> {};
struct pixel_stop_event : tcspc::base_time_tagged_event<> {};

// Workaround for https://github.com/llvm/llvm-project/issues/54668 (probably
// fixed in LLVM 18):
// NOLINTNEXTLINE(bugprone-exception-escape)
struct settings {
    std::string input_filename;
    std::string output_filename;
    channel_type sync_channel = 0;
    channel_type pixel_marker_channel = 0;
    channel_type photon_leading_channel = 0;
    channel_type photon_trailing_channel = 0;
    abstime_type sync_delay = 0;
    abstime_type max_photon_pulse_width = 100'000;
    difftime_type max_diff_time = 15'000;
    abstime_type pixel_time = -1;
    std::size_t pixels_per_line = 0;
    std::size_t lines_per_frame = 0;
    difftime_type bin_width = 50;
    bin_index_type max_bin_index = 255;
    bool cumulative = false;
    bool truncate = false;
};

template <bool Cumulative>
auto make_histo_proc(settings const &settings,
                     std::shared_ptr<tcspc::processor_context> const &ctx) {
    using namespace tcspc;
    auto writer = write_binary_stream(
        binary_file_output_stream(settings.output_filename, settings.truncate),
        std::make_shared<object_pool<std::vector<std::byte>>>(), 65536);
    if constexpr (Cumulative) {
        return histogram_elementwise_accumulate<never_event, error_on_overflow,
                                                true>(
            settings.pixels_per_line * settings.lines_per_frame,
            std::size_t(settings.max_bin_index) + 1, 65535,
            count<histogram_array_event<>>(
                ctx->tracker<count_access>("frame_counter"),
                select<event_set<concluding_histogram_array_event<>>>(
                    view_histogram_array_as_bytes<
                        concluding_histogram_array_event<>>(
                        std::move(writer)))));
    } else {
        return histogram_elementwise<error_on_overflow>(
            settings.pixels_per_line * settings.lines_per_frame,
            std::size_t(settings.max_bin_index) + 1, 65535,
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
    using device_event_vector = std::vector<swabian_tag_event>;

    // clang-format off

    auto [bin_increment_merge, start_stop_merge] =
    merge<event_set<
        bin_increment_event<>, pixel_start_event, pixel_stop_event>>(
            1024 * 1024,
    batch_bin_increments<pixel_start_event, pixel_stop_event>(
    count<bin_increment_batch_event<>>(
        ctx->tracker<count_access>("pixel_counter"),
    make_histo_proc<Cumulative>(settings, ctx))));

    auto [sync_merge, cfd_merge] =
    merge<event_set<detection_event<>>>(1024 * 1024,
    pair_all_between(
        settings.sync_channel,
        std::array{settings.photon_trailing_channel},
        settings.max_diff_time,
    select<event_set<detection_pair_event<>>>(
    time_correlate_at_stop(
    map_to_datapoints(difftime_data_mapper(),
    map_to_bins(
        linear_bin_mapper(0, settings.bin_width, settings.max_bin_index),
    std::move(bin_increment_merge)))))));

    auto sync_processor =
    delay(settings.sync_delay,
    std::move(sync_merge));

    auto photon_processor =
    pair_one_between(
        settings.photon_leading_channel,
        std::array{settings.photon_trailing_channel},
        settings.max_photon_pulse_width,
    select<event_set<detection_pair_event<>>>(
    time_correlate_at_midpoint(
    remove_time_correlation(
    recover_order<event_set<detection_event<>>>(
        std::abs(settings.max_photon_pulse_width),
    std::move(cfd_merge))))));

    auto pixel_marker_processor =
    match<detection_event<>, pixel_start_event>(always_matcher(),
    select<event_set<pixel_start_event>>(
    generate<pixel_start_event>(
        one_shot_timing_generator<pixel_stop_event>(settings.pixel_time),
    check_alternating<pixel_start_event, pixel_stop_event>(
    stop_with_error<event_set<warning_event>>(
        "pixel time is such that pixel stop occurs after next pixel start",
    std::move(start_stop_merge))))));

    return
    read_binary_stream<swabian_tag_event>(
        binary_file_input_stream(settings.input_filename),
        std::numeric_limits<std::uint64_t>::max(),
        std::make_shared<object_pool<device_event_vector>>(2, 2),
        65536,
    stop_with_error<event_set<warning_event>>("error reading input",
    dereference_pointer<std::shared_ptr<device_event_vector>>(
    unbatch<device_event_vector, swabian_tag_event>(
    count<swabian_tag_event>(ctx->tracker<count_access>("record_counter"),
    decode_swabian_tags(
    stop_with_error<event_set<
        warning_event,
        begin_lost_interval_event<>,
        end_lost_interval_event<>,
        untagged_counts_event<>>>("error in input data",
    check_monotonic(
    stop<event_set<warning_event>>("processing stopped",
    route<event_set<detection_event<>>>(
        channel_router(std::array{
            std::pair{settings.sync_channel, 0},
            std::pair{settings.photon_leading_channel, 1},
            std::pair{settings.photon_trailing_channel, 1},
            std::pair{settings.pixel_marker_channel, 2},
        }),
        std::move(sync_processor),
        std::move(photon_processor),
        std::move(pixel_marker_processor)))))))))));

    // clang-format on
}

void print_stats(settings const &settings,
                 std::shared_ptr<tcspc::processor_context> const &ctx) {
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

template <typename T>
auto parse_integer(std::string const &arg,
                   T min = std::numeric_limits<T>::min(),
                   T max = std::numeric_limits<T>::max()) -> T {
    if constexpr (std::is_unsigned_v<T> &&
                  sizeof(T) == sizeof(unsigned long long)) {
        auto const parsed = std::stoull(arg);
        if (parsed > max || parsed < min) {
            throw std::invalid_argument(
                "option value " + arg + " out of allowed range [" +
                std::to_string(min) + ", " + std::to_string(max) + "]");
        }
        return static_cast<T>(parsed);
    } else {
        auto const parsed = std::stoll(arg);
        if (parsed > static_cast<long long>(max) ||
            parsed < static_cast<long long>(min)) {
            throw std::invalid_argument(
                "option value " + arg + " out of allowed range [" +
                std::to_string(min) + ", " + std::to_string(max) + "]");
        }
        return static_cast<T>(parsed);
    }
}

template <typename T>
auto parse_integer_pair(std::string const &arg) -> std::pair<T, T> {
    auto const comma_index = arg.find(',');
    if (comma_index == std::string::npos)
        throw std::invalid_argument(
            "option value must be two integers separated by comma");
    return {parse_integer<T>(arg.substr(0, comma_index)),
            parse_integer<T>(arg.substr(comma_index + 1))};
}

template <typename GetValue>
void parse_option(settings &dest, std::string const &key, GetValue get_value) {
    try {
        if (key == "sync-channel") {
            dest.sync_channel = parse_integer<channel_type>(get_value());
        } else if (key == "pixel-marker-channel") {
            dest.pixel_marker_channel =
                parse_integer<channel_type>(get_value());
        } else if (key == "photon-channels") {
            std::tie(dest.photon_leading_channel,
                     dest.photon_trailing_channel) =
                parse_integer_pair<channel_type>(get_value());
        } else if (key == "sync-delay") {
            dest.sync_delay = parse_integer<abstime_type>(get_value());
        } else if (key == "max-photon-pulse-width") {
            dest.max_photon_pulse_width =
                parse_integer<abstime_type>(get_value(), 0);
        } else if (key == "max-diff-time") {
            dest.max_diff_time = parse_integer<difftime_type>(get_value(), 0);
        } else if (key == "pixel-time") {
            dest.pixel_time = parse_integer<abstime_type>(get_value(), 0);
        } else if (key == "width") {
            dest.pixels_per_line = parse_integer<std::size_t>(get_value(), 1);
        } else if (key == "height") {
            dest.lines_per_frame = parse_integer<std::size_t>(get_value(), 1);
        } else if (key == "bin-width") {
            dest.bin_width = parse_integer<difftime_type>(get_value(), 1);
        } else if (key == "bin-count") {
            dest.max_bin_index = static_cast<bin_index_type>(
                parse_integer<unsigned>(
                    get_value(), 1,
                    std::numeric_limits<bin_index_type>::max() + 1u) -
                1);
        } else if (key == "sum") {
            dest.cumulative = true;
        } else if (key == "overwrite") {
            dest.truncate = true;
        } else if (key == "help") {
            usage();
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::invalid_argument("unrecognized option");
        }
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
    if (ret.sync_channel == 0) {
        throw std::invalid_argument(
            "--sync-channel must be given and be nonzero");
    }
    if (ret.pixel_marker_channel == 0) {
        throw std::invalid_argument(
            "--pixel-marker-channel must be given and be nonzero");
    }
    if (ret.photon_leading_channel == 0 || ret.photon_trailing_channel == 0) {
        throw std::invalid_argument(
            "--photon-channels must be given and be a pair of non-zero channel numbers");
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
    } catch (tcspc::end_processing const &exc) {
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
    } catch (std::exception const &exc) {
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
        if (dynamic_cast<std::invalid_argument const *>(&exc) != nullptr)
            std::fputs("use --help for usage\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
