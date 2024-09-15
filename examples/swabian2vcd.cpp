/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/tcspc.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

void print_err(char const *str) {
    if (std::fputs(str, stderr) == EOF)
        std::terminate();
}

void usage() {
    print_err(R"(
Usage: swabian2vcd [options] input_file [output_file]

Convert a raw Swabian time tag dump (16 byte records) to VCD (Value Change
Dump) format, which can be viewed with tools such as GTKWave. Positive and
negative channels are treated as the rising and falling edge of the same
signal.

If output_file is not given, use stdout.

Limitation 1: Currently all channels are stored as `wire`. Most waveform
viewers will not display all events unless the rising and falling edges
strictly alternate. This should be fixed by storing each edge separately as
`event` when they do not alternate.

Limitation 2: Negative time values are not supported, because the VCD format
does not allow them.

Options:
    --channels=CHANNELS
        Select (comma-separated) channels to include. Only positive (rising
        edge) channel numbers should be given, and their negarive (falling
        edge) counterparts are automatically included. If not given, pre-scan
        the input to find all channels.
    --overwrite
        If given, overwrite output file if it exists.
    --help
        Show this usage and exit.
)");
}

using abstime_type = tcspc::default_data_types::abstime_type;
using channel_type = tcspc::default_data_types::channel_type;

struct settings {
    std::string input_filename;
    std::string output_filename; // Empty = stdout
    std::vector<channel_type> channels;
    bool truncate_output = false;
};

auto scan_for_channels(std::string const &input_filename)
    -> std::vector<channel_type> {
    using namespace tcspc;
    struct data_types {
        using datapoint_type = channel_type;
        using bin_index_type = u64;
    };
    auto ctx = context::create();
    // clang-format off
    auto chan_proc =
    read_binary_stream<swabian_tag_event>(
        binary_file_input_stream(input_filename),
        arg::max_length<u64>{std::numeric_limits<u64>::max()},
        recycling_bucket_source<swabian_tag_event>::create(),
        arg::granularity<>{65535},
    stop_with_error<type_list<warning_event>>("error reading input",
    unbatch<bucket<swabian_tag_event>>(
    decode_swabian_tags(
    select<type_list<detection_event<>>>(
    map_to_datapoints<detection_event<>, data_types>(
        channel_data_mapper<data_types>(),
    map_to_bins<data_types>(
        unique_bin_mapper<data_types>(
            ctx->tracker<unique_bin_mapper_access<channel_type>>("channels"),
            arg::max_bin_index<u64>{26}
        ),
    null_sink())))))));
    // clang-format on
    chan_proc.flush();
    return ctx->access<unique_bin_mapper_access<channel_type>>("channels")
        .values();
}

auto abs_channels(std::vector<channel_type> const &raw_chans)
    -> std::vector<channel_type> {
    std::set<channel_type> chans;
    for (auto const chan : raw_chans)
        chans.insert(std::abs(chan));
    return {chans.begin(), chans.end()};
}

template <typename Downstream> class write_vcd {
    bool wrote_header = false;
    abstime_type last_abstime = std::numeric_limits<abstime_type>::max();
    std::vector<channel_type> channels;
    Downstream downstream;

    void write_header() {
        std::string header;
        header += "$timescale 1 ps $end\n";
        header += "$scope module timetags $end\n";
        char id = 'a';
        for (auto const chan : channels) {
            header += "$var wire 1 ";
            header += (id++);
            header += " ch";
            header += std::to_string(chan);
            header += " $end\n";
        }
        header += "$upscope $end\n";
        header += "$enddefinitions $end\n";
        downstream.handle(tcspc::as_bytes(tcspc::span(header)));
        wrote_header = true;
    }

    void write_time_line(abstime_type abstime) {
        if (abstime > last_abstime ||
            last_abstime == std::numeric_limits<abstime_type>::max()) {
            if (abstime < 0)
                throw std::runtime_error("negative time is not supported");
            std::string time_line("#");
            time_line += std::to_string(abstime);
            time_line += '\n';
            downstream.handle(tcspc::as_bytes(tcspc::span(time_line)));
            last_abstime = abstime;
        }
    }

  public:
    explicit write_vcd(std::vector<channel_type> channels,
                       Downstream &&downstream)
        : channels(std::move(channels)), downstream(std::move(downstream)) {}

    void handle(tcspc::detection_event<> const &event) {
        if (not wrote_header)
            write_header();

        auto const chan = std::abs(event.channel);
        auto const chanchar = [this](channel_type ch) {
            auto it = std::find(channels.begin(), channels.end(), ch);
            if (it == channels.end())
                return '\0';
            auto const idx = std::distance(channels.begin(), it);
            return char('a' + idx);
        }(chan);
        if (chanchar) {
            write_time_line(event.abstime);
            char const change = event.channel > 0 ? '1' : '0';
            std::array<char, 3> change_line{change, chanchar, '\n'};
            downstream.handle(tcspc::as_bytes(tcspc::span(change_line)));
        }
    }

    void flush() { downstream.flush(); }
};

template <bool UseStdout> auto write_stream(settings const &settings) {
    using namespace tcspc;
    // The IEEE spec says VCD files should use LF newlines (I am told), so it
    // is appropriate that we use binary file streams (or stdout that has been
    // reopened in binary mode).
    if constexpr (UseStdout) {
        return borrowed_cfile_output_stream(stdout);
    } else {
        return binary_file_output_stream(
            settings.output_filename, arg::truncate{settings.truncate_output});
    }
}

template <bool UseStdout> auto vcd_processor(settings const &settings) {
    using namespace tcspc;
    // clang-format off
    return
    read_binary_stream<swabian_tag_event>(
        binary_file_input_stream(settings.input_filename),
        arg::max_length<u64>{std::numeric_limits<u64>::max()},
        recycling_bucket_source<swabian_tag_event>::create(),
        arg::granularity<>{65535},
    stop_with_error<type_list<warning_event>>("error reading input",
    unbatch<bucket<swabian_tag_event>>(
    decode_swabian_tags(
    stop_with_error<type_list<
        warning_event, begin_lost_interval_event<>,
        end_lost_interval_event<>, lost_counts_event<>>>(
            "error in input data",
    check_monotonic(
    stop<type_list<warning_event>>("processing stopped",
    write_vcd(settings.channels,
    write_binary_stream(
        write_stream<UseStdout>(settings),
        recycling_bucket_source<std::byte>::create(),
        arg::granularity<>{65535})))))))));
    // clang-format on
}

void run(settings const &settings) {
    if (settings.output_filename.empty())
        vcd_processor<true>(settings).flush();
    else
        vcd_processor<false>(settings).flush();
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
auto parse_pos_int_list(std::string const &arg) -> std::vector<T> {
    std::vector<T> ret;
    auto begin = arg.cbegin();
    auto const end = arg.cend();
    while (begin != end) {
        auto comma_it = std::find(begin, end, ',');
        if (std::distance(begin, comma_it) > 0)
            ret.push_back(parse_integer<T>(std::string(begin, comma_it), 1));
        if (comma_it != end)
            begin = std::next(comma_it);
        else
            begin = end;
    }
    return ret;
}

template <typename GetValue>
void parse_option(settings &dest, std::string const &key, GetValue get_value) {
    try {
        if (key == "channels") {
            dest.channels = parse_pos_int_list<channel_type>(get_value());
        } else if (key == "overwrite") {
            dest.truncate_output = true;
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
    if (positional.empty())
        throw std::invalid_argument(
            "at least one positional argument required (input file)");
    ret.input_filename = positional[0];
    if (positional.size() > 1)
        ret.output_filename = positional[1];
    if (positional.size() > 2)
        throw std::invalid_argument(
            "no more than 2 positional arguments allowed");
    if (ret.channels.size() > 26)
        throw std::invalid_argument("only 26 channels supported");
    return ret;
}

} // namespace

auto main(int argc, char *argv[]) -> int {
    try {
        std::vector<std::string> const args(std::next(argv),
                                            std::next(argv, argc));
        auto settings = parse_args(args);

        if (settings.channels.empty())
            settings.channels =
                abs_channels(scan_for_channels(settings.input_filename));

        if (settings.output_filename.empty()) {
            // In this program we never use stdout for anything other than VCD
            // output, so it is safe to switch it to binary mode (consistent
            // with our regular file output streams).
            bool const reopen_error =
#ifdef _WIN32
                _setmode(_fileno(stdout), _O_BINARY) == -1;
#else
                // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
                std::freopen(nullptr, "wb", stdout) == nullptr;
#endif
            if (reopen_error)
                throw std::runtime_error("cannot set stdout to binary mode");
        }

        run(settings);
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
