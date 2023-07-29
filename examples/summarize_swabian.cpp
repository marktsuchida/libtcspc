/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/buffer.hpp"
#include "libtcspc/check_monotonicity.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/delay.hpp"
#include "libtcspc/event_set.hpp"
#include "libtcspc/read_binary_stream.hpp"
#include "libtcspc/shared_processor.hpp"
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

    void new_channel(data_traits::channel_type chan) noexcept {
        channel_numbers.push_back(chan);
        channel_counts.push_back(1);
    }

  public:
    void
    handle_event(tcspc::detection_event<data_traits> const &event) noexcept {
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

    [[noreturn]] void handle_end(std::exception_ptr const &error) noexcept {
        int ret = EXIT_SUCCESS;
        std::ostringstream stream;

        if (error) {
            try {
                std::rethrow_exception(error);
            } catch (std::exception const &e) {
                stream << "Error: " << e.what() << '\n';
                stream << "The following counts are up to the error.\n";
                ret = EXIT_FAILURE;
            }
        }

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
        std::exit(ret);
    }
};

auto summarize(std::string const &filename) {
    using device_event_vector = std::vector<tcspc::swabian_tag_event>;

    // Create the processing graph (actually just a linear chain).
    auto proc = tcspc::read_binary_stream<tcspc::swabian_tag_event>(
        tcspc::binary_file_input_stream(std::string(filename)),
        std::numeric_limits<std::uint64_t>::max(),
        std::make_shared<tcspc::object_pool<device_event_vector>>(3, 3),
        65536, // Reader produces shared_ptr of vectors of device events.
        tcspc::dereference_pointer<
            std::shared_ptr<device_event_vector>>( // Get the vectors
                                                   // of device events.
            tcspc::unbatch<device_event_vector,
                           tcspc::swabian_tag_event>(    // Get individual
                                                         // device events.
                tcspc::decode_swabian_tags<data_traits>( // Decode device
                                                         // events into generic
                                                         // TCSPC events.
                    tcspc::check_monotonicity<
                        data_traits>( // Ensure the abstime is non-decreasing.
                        tcspc::stop_with_error<
                            tcspc::event_set<
                                tcspc::warning_event,
                                tcspc::begin_lost_interval_event<data_traits>,
                                tcspc::end_lost_interval_event<data_traits>,
                                tcspc::untagged_counts_event<data_traits>>,
                            std::runtime_error>(
                            "error in input", // Quit if anything wrong; now we
                                              // only have detection_event.
                            summarize_and_print()))))));

    proc.pump_events(); // Run it all.
}

auto main(int argc, char const *argv[]) -> int {
    std::vector<std::string> const args(std::next(argv),
                                        std::next(argv, argc));
    if (args.size() != 1) {
        std::fputs("A single argument (the filename) is required\n", stderr);
        return EXIT_FAILURE;
    }
    auto const filename = args.front();
    summarize(filename);
}
