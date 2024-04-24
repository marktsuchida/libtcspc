/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/tcspc.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct dtypes : tcspc::default_data_types {
    // BH channels are never negative; use of unsigned type simplifies checks
    // in summarize_and_print().
    using channel_type = std::uint32_t;
};

using channel_type = dtypes::channel_type;
using abstime_type = dtypes::abstime_type;

// Custom sink that counts events in all channels, and prints the results at
// the end of the stream.
class summarize_and_print {
    std::array<std::uint64_t, 16> photon_counts{};
    std::array<std::uint64_t, 4> marker_counts{};
    abstime_type last_abstime = std::numeric_limits<abstime_type>::min();

  public:
    void handle(tcspc::time_correlated_detection_event<dtypes> const &event) {
        ++photon_counts.at(event.channel);
        last_abstime = event.abstime;
    }

    void handle(tcspc::marker_event<dtypes> const &event) {
        ++marker_counts.at(event.channel);
        last_abstime = event.abstime;
    }

    void handle(tcspc::time_reached_event<dtypes> const &event) {
        last_abstime = event.abstime;
    }

    void flush() {
        std::ostringstream stream;
        stream << "Relative time of last event: \t" << last_abstime << '\n';
        for (std::size_t i = 0; i < photon_counts.size(); ++i)
            stream << "route " << i << ": \t" << photon_counts.at(i) << '\n';
        for (std::size_t i = 0; i < marker_counts.size(); ++i)
            stream << "mark " << i << ": \t" << marker_counts.at(i) << '\n';
        std::fputs(stream.str().c_str(), stdout);
    }
};

auto summarize(std::string const &filename) -> bool {
    using namespace tcspc;

    auto ctx = context::create();

    // clang-format off
    auto proc =
    read_binary_stream<bh_spc_event>(
        binary_file_input_stream(filename, 4), // Assume 4-byte .spc header.
        std::numeric_limits<std::uint64_t>::max(),
        recycling_bucket_source<bh_spc_event>::create(),
        65536,
    stop<type_list<warning_event>>("error reading input",
    unbatch<bh_spc_event>( // Get individual device events.
    count<bh_spc_event>(ctx->tracker<count_access>("counter"), // Count.
    decode_bh_spc<dtypes>( // Decode device events into generic TCSPC events.
    check_monotonic<dtypes>( // Ensure the abstime is non-decreasing.
    stop<type_list<warning_event, data_lost_event<dtypes>>>("error in data",
    summarize_and_print())))))));
    // clang-format on

    try {
        proc.flush(); // Run it all.
    } catch (end_of_processing const &exc) {
        // Explicit stop; counts were printed on flush.
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
        std::fputs("The above results are up to the error\n", stderr);
    } catch (std::exception const &exc) {
        // Other error; counts were not printed.
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
        return false;
    }
    std::fputs((std::to_string(ctx->access<count_access>("counter").count()) +
                " records decoded\n")
                   .c_str(),
               stderr);
    return true;
}

auto main(int argc, char const *argv[]) -> int {
    try {
        std::vector<std::string> const args(std::next(argv),
                                            std::next(argv, argc));
        if (args.size() != 1) {
            std::fputs("A single argument (the filename) is required\n",
                       stderr);
            return EXIT_FAILURE;
        }
        auto const &filename = args.front();
        return summarize(filename) ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (std::exception const &exc) {
        std::fputs("error: ", stderr);
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
        return EXIT_FAILURE;
    }
}
