/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/tcspc.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using abstime_type = tcspc::default_numeric_traits::abstime_type;

void print_out(char const *str) {
    if (std::fputs(str, stdout) == EOF)
        std::terminate();
}

void print_err(char const *str) {
    if (std::fputs(str, stderr) == EOF)
        std::terminate();
}

// Numeric traits for the summary histogram: a wide bin type so per-channel
// counts cannot overflow.
struct summary_traits : tcspc::default_numeric_traits {
    using bin_type = tcspc::u64;
};

// Trivial event used to inject a reset (to conclude the histogram) on flush.
struct reset_event {};

auto summarize(std::string const &filename) -> bool {
    using namespace tcspc;

    // Channels beyond this many are dropped from the summary.
    constexpr u16 max_bin_index = 255;

    auto ctx = context::create();

    // clang-format off
    auto proc =
    read_binary_stream<swabian_tag_event>(
        binary_file_input_stream(filename),
        arg::max_length{std::numeric_limits<u64>::max()},
        recycling_bucket_source<swabian_tag_event>::create(),
        arg::granularity<>{65536},
    stop<type_list<warning_event>>("error reading input",
    unbatch<bucket<swabian_tag_event>>( // Get individual device events.
    count<swabian_tag_event>(ctx->tracker<count_accessor>("counter"), // Count.
    decode_swabian_tags(
        // Decode device events into generic TCSPC events.
    check_monotonic( // Ensure the abstime is non-decreasing.
    stop<type_list<warning_event,
                   begin_lost_interval_event<>,
                   end_lost_interval_event<>,
                   lost_counts_event<>>>("error",
    // Track first/last abstime (min == first, max == last, given monotonicity).
    record_abstime_range(
        ctx->tracker<record_abstime_range_accessor<abstime_type>>("times"),
    // Count detection events per channel by histogramming the channel number.
    map_to_datapoints<detection_event<>, summary_traits>(
        channel_data_mapper<summary_traits>{},
    map_to_bins<summary_traits>(unique_bin_mapper<summary_traits>(
        ctx->tracker<unique_bin_mapper_accessor<summary_traits::datapoint_type>>(
            "channels"),
        arg::max_bin_index<u16>{max_bin_index}),
    append(reset_event{}, // Conclude the histogram on flush.
    histogram<histogram_policy::emit_concluding_events, reset_event,
              summary_traits>(
        arg::num_bins{std::size_t(max_bin_index) + 1},
        arg::max_per_bin<u64>{std::numeric_limits<u64>::max()},
        recycling_bucket_source<summary_traits::bin_type>::create(),
    record_last<concluding_histogram_event<summary_traits>>(
        ctx->tracker<record_last_accessor<
            concluding_histogram_event<summary_traits>>>("hist"),
    sink_all())))))))))))));
    // clang-format on

    auto const print_summary = [&] {
        auto range =
            ctx->access<record_abstime_range_accessor<abstime_type>>("times");
        std::ostringstream stream;
        if (auto const first = range.min(); not first) {
            stream << "No events" << '\n';
        } else {
            stream << "Time of first event: \t" << *first << '\n';
            stream << "Time of last event: \t" << *range.max() << '\n';
            auto const channels = ctx->access<unique_bin_mapper_accessor<
                summary_traits::datapoint_type>>("channels")
                                      .values();
            auto const result = ctx->access<record_last_accessor<
                concluding_histogram_event<summary_traits>>>("hist")
                                    .get();
            std::vector<std::pair<summary_traits::datapoint_type, u64>> counts;
            if (result) {
                auto const &data = result->data_bucket;
                auto const n = std::min(channels.size(), data.size());
                for (std::size_t i = 0; i < n; ++i)
                    counts.emplace_back(channels[i], data[i]);
            }
            std::sort(counts.begin(), counts.end());
            for (auto const &[chnum, chcnt] : counts)
                stream << chnum << ": \t" << chcnt << '\n';
        }
        print_out(stream.str().c_str());
    };

    try {
        proc.flush(); // Run it all.
        print_summary();
    } catch (end_of_processing const &exc) {
        // Explicit stop; the histogram was concluded by the stop's flush.
        print_summary();
        print_err(exc.what());
        print_err("\n");
        print_err("The above results are up to the error\n");
    } catch (std::exception const &exc) {
        // Other error; results are incomplete and not printed.
        print_err(exc.what());
        print_err("\n");
        return false;
    }
    print_err((std::to_string(ctx->access<count_accessor>("counter").count()) +
               " records decoded\n")
                  .c_str());
    return true;
}

} // namespace

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
