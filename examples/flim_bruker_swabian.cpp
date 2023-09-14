/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/binning.hpp"
#include "libtcspc/buffer.hpp"
#include "libtcspc/check.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/delay.hpp"
#include "libtcspc/generate.hpp"
#include "libtcspc/histogram_elementwise.hpp"
#include "libtcspc/match.hpp"
#include "libtcspc/merge.hpp"
#include "libtcspc/pair.hpp"
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

template <bool Cumulative> auto make_histo_proc(settings const &settings) {
    using namespace tcspc;
    auto writer = write_binary_stream(
        binary_file_output_stream(settings.output_filename, settings.truncate),
        std::make_shared<object_pool<std::vector<std::byte>>>(), 65536);
    if constexpr (Cumulative) {
        return histogram_elementwise_accumulate<
            default_data_traits, never_event, error_on_overflow, true>(
            settings.pixels_per_line * settings.lines_per_frame,
            std::size_t(settings.max_bin_index) + 1, 65535,
            select<event_set<concluding_histogram_array_event<>>>(
                view_histogram_array_as_bytes<
                    concluding_histogram_array_event<>>(std::move(writer))));
    } else {
        return histogram_elementwise<default_data_traits, error_on_overflow>(
            settings.pixels_per_line * settings.lines_per_frame,
            std::size_t(settings.max_bin_index) + 1, 65535,
            select<event_set<histogram_array_event<>>>(
                view_histogram_array_as_bytes<histogram_array_event<>>(
                    std::move(writer))));
    }
}

template <bool Cumulative> auto make_processor(settings const &settings) {
    using namespace tcspc;
    using device_event_vector = std::vector<swabian_tag_event>;
    struct pixel_start_event : base_time_tagged_event<> {};
    struct pixel_stop_event : base_time_tagged_event<> {};

    // clang-format off

    auto [bin_increment_merge, start_stop_merge] =
    merge<default_data_traits, event_set<
        bin_increment_event<>, pixel_start_event, pixel_stop_event>>(
            settings.max_photon_pulse_width,
    batch_bin_increments<
        default_data_traits, pixel_start_event, pixel_stop_event>(
    make_histo_proc<Cumulative>(settings)));

    auto [sync_merge, cfd_merge] =
    merge<default_data_traits, event_set<detection_event<>>>(
        std::abs(settings.sync_delay),
    pair_all_between<default_data_traits>(
        settings.sync_channel,
        std::array<channel_type, 1>{settings.photon_trailing_channel},
        settings.max_diff_time,
    select<event_set<detection_pair_event<>>>(
    time_correlate_at_stop(
    map_to_datapoints(
        difftime_data_mapper(),
    map_to_bins(
        linear_bin_mapper(0, settings.bin_width, settings.max_bin_index),
    std::move(bin_increment_merge)))))));

    auto sync_processor =
    delay<default_data_traits>(settings.sync_delay,
    std::move(sync_merge));

    auto [photon_leading_processor, photon_trailing_processor] =
    merge_n_unsorted(
    pair_one_between<default_data_traits>(
        settings.photon_leading_channel,
        std::array<channel_type, 1>{settings.photon_trailing_channel},
        settings.max_photon_pulse_width,
    select<event_set<detection_pair_event<>>>(
    time_correlate_at_midpoint(
    remove_time_correlation(
    recover_order<default_data_traits, detection_event<>>(
        std::abs(settings.max_photon_pulse_width),
    std::move(cfd_merge)))))));

    auto pixel_marker_processor =
    match<detection_event<>, pixel_start_event>(
        []([[maybe_unused]] detection_event<> const &event) { return true; },
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
    stop_with_error<event_set<warning_event>>(
        "error reading input",
    dereference_pointer<std::shared_ptr<device_event_vector>>(
    unbatch<device_event_vector, swabian_tag_event>(
    decode_swabian_tags(
    stop_with_error<event_set<
        warning_event,
        begin_lost_interval_event<>,
        end_lost_interval_event<>,
        untagged_counts_event<>>>(
            "error in input data",
    check_monotonic(
    stop<event_set<warning_event>>( // Stop quietly on non-monotonic time.
    route<event_set<detection_event<>>>(
        channel_router(std::array<channel_type, 4>{
            settings.sync_channel,
            settings.photon_leading_channel,
            settings.photon_trailing_channel,
            settings.pixel_marker_channel,
        }),
        std::move(sync_processor),
        std::move(photon_leading_processor),
        std::move(photon_trailing_processor),
        std::move(pixel_marker_processor))))))))));
    // clang-format on
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
auto parse_integer_pair(std::string const &arg) -> std::array<T, 2> {
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
            make_processor<true>(settings).pump_events();
        else
            make_processor<false>(settings).pump_events();
    } catch (tcspc::end_processing const &) {
        // Ok.
    } catch (std::exception const &exc) {
        std::fputs("Error: ", stderr);
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
        if (dynamic_cast<std::invalid_argument const *>(&exc) != nullptr)
            std::fputs("Use --help for usage\n", stderr);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
