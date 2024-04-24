/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/tcspc.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using channel_type = tcspc::default_data_types::channel_type;
using abstime_type = tcspc::default_data_types::abstime_type;

void print_out(char const *str) {
    if (std::fputs(str, stdout) == EOF)
        std::terminate();
}

void print_err(char const *str) {
    if (std::fputs(str, stderr) == EOF)
        std::terminate();
}

// Custom sink that counts detection events in all channels encountered, and
// prints the results at the end of the stream.
class summarize_and_print {
    std::vector<channel_type> channel_numbers;
    std::vector<std::uint64_t> channel_counts;
    abstime_type first_abstime = std::numeric_limits<abstime_type>::min();
    abstime_type last_abstime = std::numeric_limits<abstime_type>::min();

    void new_channel(channel_type chan) {
        channel_numbers.push_back(chan);
        channel_counts.push_back(1);
    }

  public:
    void handle(tcspc::detection_event<> const &event) {
        auto const p = std::find(channel_numbers.begin(),
                                 channel_numbers.end(), event.channel);
        if (p == channel_numbers.end())
            new_channel(event.channel);
        else
            ++*std::next(channel_counts.begin(),
                         std::distance(channel_numbers.begin(), p));
        if (first_abstime == std::numeric_limits<abstime_type>::min())
            first_abstime = event.abstime;
        last_abstime = event.abstime;
    }

    void flush() {
        std::ostringstream stream;

        if (channel_numbers.empty()) {
            stream << "No events" << '\n';
        } else {
            stream << "Time of first event: \t" << first_abstime << '\n';
            stream << "Time of last event: \t" << last_abstime << '\n';
            // Print channel counts in channel number order.
            std::vector<std::pair<channel_type, std::uint64_t>> counts(
                channel_numbers.size());
            std::transform(channel_numbers.cbegin(), channel_numbers.cend(),
                           channel_counts.cbegin(), counts.begin(),
                           [](auto chnum, auto chcnt) {
                               return std::pair{chnum, chcnt};
                           });
            std::sort(counts.begin(), counts.end());
            for (auto const &[chnum, chcnt] : counts)
                stream << chnum << ": \t" << chcnt << '\n';
        }

        print_out(stream.str().c_str());
    }
};

auto summarize(std::string const &filename) -> bool {
    using namespace tcspc;

    auto ctx = context::create();

    // clang-format off
    auto proc =
    read_binary_stream<swabian_tag_event>(
        binary_file_input_stream(filename),
        std::numeric_limits<std::uint64_t>::max(),
        recycling_bucket_source<swabian_tag_event>::create(),
        65536,
    stop<type_list<warning_event>>("error reading input",
    unbatch<swabian_tag_event>( // Get individual device events.
    count<swabian_tag_event>(ctx->tracker<count_access>("counter"), // Count.
    decode_swabian_tags(
        // Decode device events into generic TCSPC events.
    check_monotonic( // Ensure the abstime is non-decreasing.
    stop<type_list<warning_event,
                   begin_lost_interval_event<>,
                   end_lost_interval_event<>,
                   lost_counts_event<>>>("error",
    summarize_and_print())))))));
    // clang-format on

    try {
        proc.flush(); // Run it all.
    } catch (end_of_processing const &exc) {
        // Explicit stop; counts were printed on flush.
        print_err(exc.what());
        print_err("\n");
        print_err("The above results are up to the error\n");
    } catch (std::exception const &exc) {
        // Other error; counts were not printed.
        print_err(exc.what());
        print_err("\n");
        return false;
    }
    print_err((std::to_string(ctx->access<count_access>("counter").count()) +
               " records decoded\n")
                  .c_str());
    return true;
}

auto main(int argc, char const *argv[]) -> int {
    try {
        std::vector<std::string> const args(std::next(argv),
                                            std::next(argv, argc));
        if (args.size() != 1) {
            print_err("A single argument (the filename) is required\n");
            return EXIT_FAILURE;
        }
        auto const &filename = args.front();
        return summarize(filename) ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (std::exception const &exc) {
        print_err("error: ");
        print_err(exc.what());
        print_err("\n");
        return EXIT_FAILURE;
    }
}
