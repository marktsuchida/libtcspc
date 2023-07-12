/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "../BHSPCFile.hpp"
#include "libtcspc/bh_spc.hpp"
#include "libtcspc/buffer.hpp"
#include "libtcspc/legacy_histogram.hpp"
#include "libtcspc/line_clock_pixellator.hpp"

#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

void usage() {
    std::cerr
        << "Test driver for histogramming.\n"
        << "Usage: SPCToHistogram <width> <height> <line_delay> <line_time> input.spc output.raw\n"
        << "where <line_delay> and <line_time> are in macrotime units.\n"
        << "Currently the output contains only the raw cumulative histogram.\n";
}

// Not sure why this warning is issued on this line, but suppress.
// NOLINTNEXTLINE(bugprone-exception-escape)
template <typename T> class histogram_saver {
    std::size_t frame_count = 0;
    std::string out_filename;

  public:
    explicit histogram_saver(std::string out_filename)
        : out_filename(std::move(out_filename)) {}

    void handle_end(std::exception_ptr const &error) {
        if (error) {
            try {
                std::rethrow_exception(error);
            } catch (std::exception const &e) {
                std::cerr << e.what() << '\n';
                std::exit(1);
            }
        }
    }

    void handle_event(tcspc::frame_histogram_event<T> const &event) {
        (void)event;
        std::cerr << "Frame " << (frame_count++) << '\n';
    }

    void
    handle_event(tcspc::final_cumulative_histogram_event<T> const &event) {
        if (frame_count == 0) { // No frames
            std::cerr << "No frames\n";
            return;
        }

        std::fstream output(out_filename,
                            std::fstream::binary | std::fstream::out);
        if (!output.is_open()) {
            std::cerr << "Cannot open " << out_filename << '\n';
            std::exit(1);
        }

        auto &histogram = event.histogram;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        output.write(reinterpret_cast<const char *>(histogram.get()),
                     static_cast<std::streamsize>(
                         histogram.get_number_of_elements() * sizeof(T)));
    }
};

// Error handling is not implemented in this example.
// NOLINTNEXTLINE(bugprone-exception-escape)
auto main(int argc, char *argv[]) -> int {
    if (argc != 7) {
        usage();
        return 1;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::uint32_t width = 0;
    std::istringstream(argv[1]) >> width;
    std::uint32_t height = 0;
    std::istringstream(argv[2]) >> height;
    std::uint32_t line_delay = 0;
    std::istringstream(argv[3]) >> line_delay;
    std::uint32_t line_time = 0;
    std::istringstream(argv[4]) >> line_time;
    std::string const in_filename(argv[5]);
    std::string const out_filename(argv[6]);
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    std::uint32_t const max_frames = UINT32_MAX;

    using sample_type = std::uint16_t;
    std::uint32_t const input_bits = 12;
    std::uint32_t const histo_bits = 8;
    tcspc::legacy_histogram<sample_type> frame_histo(histo_bits, input_bits,
                                                     true, width, height);
    tcspc::legacy_histogram<sample_type> cumul_histo(histo_bits, input_bits,
                                                     true, width, height);
    cumul_histo.clear();

    // Construct pipeline
    // clang-format off
    auto buffer =
        tcspc::buffer_event<
                std::shared_ptr<std::vector<tcspc::bh_spc_event>>>(
        tcspc::dereference_pointer<
                std::shared_ptr<std::vector<tcspc::bh_spc_event>>>(
        tcspc::unbatch<
                std::vector<tcspc::bh_spc_event>, tcspc::bh_spc_event>(
        tcspc::decode_bh_spc<tcspc::default_data_traits>(
        tcspc::line_clock_pixellator(
                width, height, max_frames, static_cast<std::int32_t>(line_delay), line_time, 1,
        tcspc::sequential_histogrammer(std::move(frame_histo),
        tcspc::histogram_accumulator(std::move(cumul_histo),
        histogram_saver<sample_type>(out_filename))))))));
    // clang-format on

    std::thread proc_thread([&] { buffer.pump_downstream(); });

    std::fstream input(in_filename, std::fstream::binary | std::fstream::in);
    if (!input.is_open()) {
        std::cerr << "Cannot open " << in_filename << '\n';
        return 1;
    }

    tcspc::object_pool<std::vector<tcspc::bh_spc_event>> pool(2);

    constexpr std::size_t batch_capacity = std::size_t(48) * 1024;
    constexpr auto max_size = batch_capacity * sizeof(tcspc::bh_spc_event);

    input.seekg(sizeof(bh_spc_file_header));
    while (input.good()) {
        auto arr = pool.check_out();
        arr->resize(batch_capacity);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        input.read(reinterpret_cast<char *>(arr->data()), max_size);
        auto const readSize = static_cast<std::size_t>(input.gcount()) /
                              sizeof(tcspc::bh_spc_event);
        arr->resize(static_cast<std::size_t>(readSize));
        buffer.handle_event(arr);
    }
    buffer.handle_end({});

    proc_thread.join();
    return 0;
}
