/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/bh_spc.hpp"
#include "libtcspc/buffer.hpp"
#include "libtcspc/check.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/read_binary_stream.hpp"
#include "libtcspc/stop.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct dtraits : tcspc::default_data_traits {
    // BH channels are never negative; use of unsigned type simplifies checks
    // in summarize_and_print().
    using channel_type = std::uint32_t;
};

using channel_type = dtraits::channel_type;
using abstime_type = dtraits::abstime_type;

// Custom sink that counts events in all channels, and prints the results at
// the end of the stream.
class summarize_and_print {
    std::array<std::uint64_t, 16> photon_counts{};
    std::array<std::uint64_t, 4> marker_counts{};
    abstime_type last_abstime = std::numeric_limits<abstime_type>::min();

  public:
    void handle(tcspc::time_correlated_detection_event<dtraits> const &event) {
        ++photon_counts.at(event.channel);
        last_abstime = event.abstime;
    }

    void handle(tcspc::marker_event<dtraits> const &event) {
        ++marker_counts.at(event.channel);
        last_abstime = event.abstime;
    }

    void handle(tcspc::time_reached_event<dtraits> const &event) {
        last_abstime = event.abstime;
    }

    void flush() {
        std::ostringstream stream;
        stream << "Relative time of last event: \t" << last_abstime << '\n';
        for (std::size_t i = 0; i < photon_counts.size(); ++i)
            stream << "rout " << i << ": \t" << photon_counts.at(i) << '\n';
        for (std::size_t i = 0; i < marker_counts.size(); ++i)
            stream << "mark " << i << ": \t" << marker_counts.at(i) << '\n';
        std::fputs(stream.str().c_str(), stdout);
    }
};

auto summarize(std::string const &filename) -> bool {
    using namespace tcspc;
    using device_event_vector = std::vector<bh_spc_event>;

    // clang-format off
    auto proc =
    read_binary_stream<bh_spc_event>(
        binary_file_input_stream(filename, 4), // Assume 4-byte .spc header.
        std::numeric_limits<std::uint64_t>::max(),
        std::make_shared<object_pool<device_event_vector>>(3, 3),
        65536, // Reader produces shared_ptr of vectors of device events.
    stop<event_set<warning_event>>( // End processing on read error.
    dereference_pointer<std::shared_ptr<device_event_vector>>(
        // Get the vectors of device events.
    unbatch<device_event_vector, bh_spc_event>(
        // Get individual device events.
    decode_bh_spc<dtraits>( // Decode device events into generic TCSPC events.
    check_monotonic<dtraits>( // Ensure the abstime is non-decreasing.
    stop<event_set<warning_event, data_lost_event<dtraits>>>(
        // End processing if anything wrong.
    summarize_and_print())))))));
    // clang-format on

    try {
        proc.pump_events(); // Run it all.
    } catch (end_processing const &) {
        // Explicit stop; counts were printed on flush.
        // TODO Recover the error message (needs to be packaged in exc)
        std::fputs("\n", stderr);
        std::fputs("Error encountered in input\n", stderr);
        std::fputs("The above results are only up to the error\n", stderr);
        // TODO Determine byte position in input stream of error.
        return false;
    } catch (std::exception const &exc) {
        // Other error; counts were not printed.
        std::fputs("Error: ", stderr);
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
        return false;
    }
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
        std::fputs("Error: ", stderr);
        std::fputs(exc.what(), stderr);
        std::fputs("\n", stderr);
        return EXIT_FAILURE;
    }
}
