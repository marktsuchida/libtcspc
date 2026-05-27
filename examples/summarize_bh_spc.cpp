/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/tcspc.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct numtraits : tcspc::default_numeric_traits {
    // BH channels are never negative.
    using channel_type = tcspc::u32;
};

using abstime_type = numtraits::abstime_type;

void print_out(char const *str) {
    if (std::fputs(str, stdout) == EOF)
        std::terminate();
}

void print_err(char const *str) {
    if (std::fputs(str, stderr) == EOF)
        std::terminate();
}

// Numeric traits for the summary histograms: a wide bin type so counts cannot
// overflow, and a datapoint type wide enough to hold the channel without
// narrowing.
struct summary_traits : numtraits {
    using datapoint_type = tcspc::u32;
    using bin_type = tcspc::u64;
};

// Trivial event used to inject a reset (to conclude the histograms) on flush.
struct reset_event {};

auto summarize(std::string const &filename) -> bool {
    using namespace tcspc;

    auto ctx = context::create();

    // clang-format off
    auto proc =
    read_binary_stream<bh_spc_event>(
        // Assume 4-byte .spc header:
        binary_file_input_stream(filename, arg::start_offset<u64>{4}),
        arg::max_length{std::numeric_limits<u64>::max()},
        recycling_bucket_source<bh_spc_event>::create(),
        arg::granularity<>{65536},
    stop<type_list<warning_event>>("error reading input",
    unbatch<bucket<bh_spc_event>>( // Get individual device events.
    count<bh_spc_event>(ctx->tracker<count_accessor>("counter"), // Count.
    decode_bh_spc<numtraits>( // Decode device events into generic TCSPC events.
    check_monotonic<numtraits>( // Ensure the abstime is non-decreasing.
    stop<type_list<warning_event, data_lost_event<numtraits>>>("error in data",
    // Track the last abstime (max == last, given monotonicity).
    record_abstime_range<numtraits>(
        ctx->tracker<record_abstime_range_accessor<abstime_type>>("times"),
    // The two event types use separate channel spaces, so histogram each in its
    // own branch; each branch's map_to_datapoints converts only its own type.
    broadcast<type_list<time_correlated_detection_event<numtraits>,
                        marker_event<numtraits>,
                        time_reached_event<numtraits>>>(
        // Count photons per route (0..15).
        map_to_datapoints<time_correlated_detection_event<numtraits>,
                          summary_traits>(
            channel_data_mapper<summary_traits>{},
        map_to_bins<summary_traits>(linear_bin_mapper<summary_traits>(
            arg::offset<summary_traits::datapoint_type>{0},
            arg::bin_width<summary_traits::datapoint_type>{1},
            arg::max_bin_index<summary_traits::bin_index_type>{15}),
        append(reset_event{},
        histogram<histogram_policy::emit_concluding_events, reset_event,
                  summary_traits>(
            arg::num_bins{std::size_t{16}},
            arg::max_per_bin<u64>{std::numeric_limits<u64>::max()},
            recycling_bucket_source<summary_traits::bin_type>::create(),
        record_last<concluding_histogram_event<summary_traits>>(
            ctx->tracker<record_last_accessor<
                concluding_histogram_event<summary_traits>>>("photons"),
        sink_all()))))),
        // Count markers per channel (0..3).
        map_to_datapoints<marker_event<numtraits>, summary_traits>(
            channel_data_mapper<summary_traits>{},
        map_to_bins<summary_traits>(linear_bin_mapper<summary_traits>(
            arg::offset<summary_traits::datapoint_type>{0},
            arg::bin_width<summary_traits::datapoint_type>{1},
            arg::max_bin_index<summary_traits::bin_index_type>{3}),
        append(reset_event{},
        histogram<histogram_policy::emit_concluding_events, reset_event,
                  summary_traits>(
            arg::num_bins{std::size_t{4}},
            arg::max_per_bin<u64>{std::numeric_limits<u64>::max()},
            recycling_bucket_source<summary_traits::bin_type>::create(),
        record_last<concluding_histogram_event<summary_traits>>(
            ctx->tracker<record_last_accessor<
                concluding_histogram_event<summary_traits>>>("markers"),
        sink_all()))))))))))))));
    // clang-format on

    auto const print_summary = [&] {
        auto range =
            ctx->access<record_abstime_range_accessor<abstime_type>>("times");
        auto const photons = ctx->access<record_last_accessor<
            concluding_histogram_event<summary_traits>>>("photons")
                                 .get();
        auto const markers = ctx->access<record_last_accessor<
            concluding_histogram_event<summary_traits>>>("markers")
                                 .get();

        std::ostringstream stream;
        stream << "Relative time of last event: \t"
               << range.max().value_or(
                      std::numeric_limits<abstime_type>::min())
               << '\n';
        for (std::size_t i = 0; i < 16; ++i)
            stream << "route " << i << ": \t"
                   << (photons ? photons->data_bucket[i] : 0) << '\n';
        for (std::size_t i = 0; i < 4; ++i)
            stream << "mark " << i << ": \t"
                   << (markers ? markers->data_bucket[i] : 0) << '\n';
        print_out(stream.str().c_str());
    };

    try {
        proc.flush(); // Run it all.
        print_summary();
    } catch (end_of_processing const &exc) {
        // Explicit stop; the histograms were concluded by the stop's flush.
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
