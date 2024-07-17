/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/tcspc.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <limits>
#include <string>

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
Usage: resonant_flim_swabian options input_file output_file

This is a work in progress.

- For now, pixels are linear starting at line marker (after applying delay).
- No correction is performed for bidirectional scanning and linearization.

The input file must be a raw dump of Swabian 16-byte records.

Options:
    --sync-channel=CHANNEL
        Specify the channel containing the laser sync signal (required)
    --photon-channels=LEADING,TRAILING
        Specify the two channels containing the leading and trailing (often,
        falling and rising) edges of the photon pulses (required)
    --frame-channel=CHANNEL
        Specify the channel containing the frame start merker (required)
    --line-channel=CHANNEL
        Specify the channel containing the line marker (required)
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
    --dump-graph
        Do not process input; instead emit the processing graph to standard
        output in Graphviz dot format
    --help
        Show this usage and exit
)");
}

using abstime_type = tcspc::default_data_types::abstime_type;
using difftime_type = tcspc::default_data_types::difftime_type;
using channel_type = tcspc::default_data_types::channel_type;
using bin_index_type = tcspc::default_data_types::bin_index_type;
using bin_type = tcspc::default_data_types::bin_type;

struct frame_start_event {
    abstime_type abstime;
};

struct frame_stop_event {
    abstime_type abstime;
};

struct line_event {
    abstime_type abstime;
};

struct pixel_start_event {
    abstime_type abstime;
};

struct pixel_stop_event {
    abstime_type abstime;
};

struct settings {
    std::string input_filename;
    std::string output_filename;
    channel_type sync_channel{};
    channel_type photon_leading_channel{};
    channel_type photon_trailing_channel{};
    channel_type frame_channel{};
    channel_type line_channel{};
    abstime_type sync_delay{};
    abstime_type line_delay{};
    abstime_type max_photon_pulse_width = 100'000;
    difftime_type max_diff_time = 15'000;
    abstime_type pixel_time = -1; // Temporarily use constant
    std::size_t pixels_per_line{};
    std::size_t lines_per_frame{};
    difftime_type bin_width = 50;
    bin_index_type max_bin_index = 255;
    bool cumulative = false;
    bool truncate_output = false;
    bool dump_graph = false;
};

template <bool Cumulative>
auto make_histo_proc(settings const &settings,
                     std::shared_ptr<tcspc::context> const &ctx) {
    using namespace tcspc;
    auto bsource = recycling_bucket_source<bin_type>::create();
    auto writer = write_binary_stream(
        binary_file_output_stream(settings.output_filename,
                                  arg::truncate{settings.truncate_output}),
        recycling_bucket_source<std::byte>::create(),
        arg::granularity<std::size_t>{65536});
    struct reset_event {};
    if constexpr (Cumulative) {
        return append(
            reset_event{}, // Reset before flush to get concluding array.
            histogram_scans<histogram_policy::emit_concluding_events,
                            reset_event>(
                arg::num_elements{settings.pixels_per_line *
                                  settings.lines_per_frame},
                arg::num_bins{std::size_t(settings.max_bin_index) + 1},
                arg::max_per_bin<bin_type>{65535}, bsource,
                count<histogram_array_event<>>(
                    ctx->tracker<count_access>("frame_counter"),
                    select<type_list<concluding_histogram_array_event<>>>(
                        extract_bucket<concluding_histogram_array_event<>>(
                            view_as_bytes(std::move(writer)))))));
    } else {
        return histogram_scans<histogram_policy::clear_every_scan>(
            arg::num_elements{settings.pixels_per_line *
                              settings.lines_per_frame},
            arg::num_bins{std::size_t(settings.max_bin_index) + 1},
            arg::max_per_bin<bin_type>{65535}, bsource,
            select<type_list<histogram_array_event<>>>(
                count<histogram_array_event<>>(
                    ctx->tracker<count_access>("frame_counter"),
                    extract_bucket<histogram_array_event<>>(
                        view_as_bytes(std::move(writer))))));
    }
}

template <bool Cumulative>
auto make_processor(settings const &settings,
                    std::shared_ptr<tcspc::context> const &ctx) {
    using namespace tcspc;
    struct reset_event{};

    // clang-format off

    auto [tc_merge, pixel_merge] =
    merge<type_list<
        time_correlated_detection_event<>,
        pixel_start_event,
        pixel_stop_event>>(
            arg::max_buffered<std::size_t>{1 << 20},
    map_to_datapoints<time_correlated_detection_event<>>(
        difftime_data_mapper(),
    map_to_bins(
        linear_bin_mapper(
            arg::offset{0},
            arg::bin_width{settings.bin_width},
            arg::max_bin_index{settings.max_bin_index}),
    batch_bin_increments<pixel_start_event, pixel_stop_event>(
    count<bin_increment_batch_event<>>(
        ctx->tracker<count_access>("pixel_counter"),
    make_histo_proc<Cumulative>(settings, ctx))))));

    auto [sync_merge, cfd_merge] =
    merge<type_list<detection_event<>>>(
        arg::max_buffered<std::size_t>{1 << 30},
    pair_all_between(
        arg::start_channel{settings.sync_channel},
        std::array{settings.photon_trailing_channel},
        arg::time_window<abstime_type>{settings.max_diff_time},
    select<type_list<std::array<detection_event<>, 2>>>(
    time_correlate_at_stop(
    std::move(tc_merge)))));

    auto sync_processor =
    delay(arg::delta{settings.sync_delay},
    std::move(sync_merge));

    auto photon_processor =
    pair_one_between(
        arg::start_channel{settings.photon_leading_channel},
        std::array{settings.photon_trailing_channel},
        arg::time_window{settings.max_photon_pulse_width},
    select<type_list<std::array<detection_event<>, 2>>>(
    time_correlate_at_midpoint(
    remove_time_correlation(
    recover_order<type_list<detection_event<>>>(
        arg::time_window{std::abs(settings.max_photon_pulse_width)},
    std::move(cfd_merge))))));

    auto marker_processor =
    match_replace<detection_event<>, frame_start_event>(
        channel_matcher(arg::channel<channel_type>{settings.frame_channel}),
    stop_with_error<type_list<warning_event>>(
        "frame start and stop do not alternate",
    match_replace<detection_event<>, line_event>(
        channel_matcher(arg::channel<channel_type>{settings.line_channel}),
    select<type_list<frame_start_event, frame_stop_event, line_event>>(

    count_up_to<line_event, frame_stop_event, frame_start_event, true>(
        arg::threshold<u64>{settings.lines_per_frame},
        arg::limit<u64>{settings.lines_per_frame},
        arg::initial_count<u64>{0},
    gate<type_list<line_event>, frame_start_event, frame_stop_event>(
        arg::initially_open{false},
    select<type_list<line_event>>(
    delay(arg::delta{settings.line_delay},
    generate<line_event, pixel_start_event>(
        linear_timing_generator<>(
            arg::delay<abstime_type>{0},
            arg::interval<abstime_type>{settings.pixel_time},
            arg::count<std::size_t>{settings.pixels_per_line}
        ),
    generate<line_event, pixel_stop_event>(
        linear_timing_generator<>(
            arg::delay<abstime_type>{settings.pixel_time},
            arg::interval<abstime_type>{settings.pixel_time},
            arg::count<std::size_t>{settings.pixels_per_line}
        ),
    select_not<type_list<line_event>>(
    check_alternating<pixel_start_event, pixel_stop_event>(
    stop_with_error<type_list<warning_event>>(
        "pixel start and stop do not alternate",
    std::move(pixel_merge))))))))))))));

    return
    read_binary_stream<swabian_tag_event>(
        binary_file_input_stream(settings.input_filename),
        arg::max_length{std::numeric_limits<u64>::max()},
        recycling_bucket_source<swabian_tag_event>::create(),
        arg::granularity<std::size_t>{65536},
    stop_with_error<type_list<warning_event>>("error reading input",
    unbatch<swabian_tag_event>(
    count<swabian_tag_event>(ctx->tracker<count_access>("record_counter"),
    decode_swabian_tags(
    check_monotonic(
    stop_with_error<type_list<
        warning_event,
        begin_lost_interval_event<>,
        end_lost_interval_event<>,
        lost_counts_event<>>>("error in input data",
    route<type_list<detection_event<>>>(
        channel_router(std::array{
            std::pair{settings.sync_channel, 0},
            std::pair{settings.photon_leading_channel, 1},
            std::pair{settings.photon_trailing_channel, 1},
            std::pair{settings.frame_channel, 2},
            std::pair{settings.line_channel, 2},
        }),
    std::move(sync_processor),
    std::move(photon_processor),
    std::move(marker_processor)))))))));

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

template <bool Cumulative> void run(settings const &settings) {
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
        } else if (key == "photon-channels") {
            std::tie(dest.photon_leading_channel,
                     dest.photon_trailing_channel) =
                parse_integer_pair<channel_type>(get_value());
        } else if (key == "frame-channel") {
            dest.frame_channel = parse_integer<channel_type>(get_value());
        } else if (key == "line-channel") {
            dest.line_channel = parse_integer<channel_type>(get_value());
        } else if (key == "sync-delay") {
            dest.sync_delay = parse_integer<abstime_type>(get_value());
        } else if (key == "line-delay") {
            dest.line_delay = parse_integer<abstime_type>(get_value());
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
            dest.truncate_output = true;
        } else if (key == "dump-graph") {
            dest.dump_graph = true;
        } else if (key == "help") {
            usage();
            std::_Exit(EXIT_SUCCESS);
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
    if (ret.photon_leading_channel == 0 || ret.photon_trailing_channel == 0) {
        throw std::invalid_argument(
            "--photon-channels must be given and be a pair of non-zero channel numbers");
    }
    if (ret.frame_channel == 0) {
        throw std::invalid_argument(
            "--frame-channel must be given and be nonzero");
    }
    if (ret.line_channel == 0) {
        throw std::invalid_argument(
            "--line-channel must be given and be nonzero");
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
            run<true>(settings);
        else
            run<false>(settings);
    } catch (tcspc::end_of_processing const &exc) {
        print_err(exc.what());
        print_err("\n");
    } catch (std::exception const &exc) {
        print_err(exc.what());
        print_err("\n");
        if (dynamic_cast<std::invalid_argument const *>(&exc) != nullptr)
            print_err("use --help for usage\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
