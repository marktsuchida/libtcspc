/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/buffer.hpp"
#include "libtcspc/check.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/delay.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/read_binary_stream.hpp"
#include "libtcspc/stop.hpp"
#include "libtcspc/swabian_tag.hpp"

#include <algorithm>
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

struct data_traits : tcspc::default_data_traits {
    using channel_type = std::int32_t;
};

// Custom sink that counts detection events in all channels encountered, and
// prints the results at the end of the stream. For simplicity, it exits the
// program when finished.
class summarize_and_print {
    std::vector<data_traits::channel_type> channel_numbers;
    std::vector<std::uint64_t> channel_counts;
    data_traits::abstime_type first_abstime =
        std::numeric_limits<data_traits::abstime_type>::min();
    data_traits::abstime_type last_abstime =
        std::numeric_limits<data_traits::abstime_type>::min();

    void new_channel(data_traits::channel_type chan) {
        channel_numbers.push_back(chan);
        channel_counts.push_back(1);
    }

  public:
    void handle(tcspc::detection_event<data_traits> const &event) {
        auto const p = std::find(channel_numbers.begin(),
                                 channel_numbers.end(), event.channel);
        if (p == channel_numbers.end())
            new_channel(event.channel);
        else
            ++*std::next(channel_counts.begin(),
                         std::distance(channel_numbers.begin(), p));
        if (first_abstime ==
            std::numeric_limits<decltype(first_abstime)>::min())
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
            std::vector<std::tuple<data_traits::channel_type, std::uint64_t>>
                counts(channel_numbers.size());
            std::transform(channel_numbers.cbegin(), channel_numbers.cend(),
                           channel_counts.cbegin(), counts.begin(),
                           [](auto chnum, auto chcnt) {
                               return std::make_tuple(chnum, chcnt);
                           });
            std::sort(counts.begin(), counts.end());
            for (auto const &[chnum, chcnt] : counts)
                stream << chnum << ": \t" << chcnt << '\n';
        }

        std::fputs(stream.str().c_str(), stdout);
    }
};

auto summarize(std::string const &filename) -> bool {
    using namespace tcspc;
    using device_event_vector = std::vector<swabian_tag_event>;

    // clang-format off
    auto proc =
    read_binary_stream<swabian_tag_event>(
        binary_file_input_stream(filename),
        std::numeric_limits<std::uint64_t>::max(),
        std::make_shared<object_pool<device_event_vector>>(3, 3),
        65536, // Reader produces shared_ptr of vectors of device events.
    stop<event_set<warning_event>>( // End processing on read error.
    dereference_pointer<std::shared_ptr<device_event_vector>>(
        // Get the vectors of device events.
    unbatch<device_event_vector, swabian_tag_event>(
        // Get individual device events.
    decode_swabian_tags<data_traits>(
        // Decode device events into generic TCSPC events.
    check_monotonic<data_traits>( // Ensure the abstime is non-decreasing.
    stop<event_set<warning_event,
                   begin_lost_interval_event<data_traits>,
                   end_lost_interval_event<data_traits>,
                   untagged_counts_event<data_traits>>>(
        // End processing if anything wrong; now we only have detection_event.
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
