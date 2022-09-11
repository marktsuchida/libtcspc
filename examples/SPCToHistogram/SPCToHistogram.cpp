/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "../BHSPCFile.hpp"
#include "FLIMEvents/BHDeviceEvents.hpp"
#include "FLIMEvents/Buffer.hpp"
#include "FLIMEvents/LegacyHistogram.hpp"
#include "FLIMEvents/LineClockPixellator.hpp"

#include <ctime>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

void Usage() {
    std::cerr
        << "Test driver for histogramming.\n"
        << "Usage: SPCToHistogram <width> <height> <lineDelay> <lineTime> input.spc output.raw\n"
        << "where <lineDelay> and <lineTime> are in macrotime units.\n"
        << "Currently the output contains only the raw cumulative histogram.\n";
}

template <typename T> class HistogramSaver {
    std::size_t frameCount;
    std::string const &outFilename;

  public:
    explicit HistogramSaver(std::string const &outFilename)
        : frameCount(0), outFilename(outFilename) {}

    void handle_end(std::exception_ptr error) {
        if (error) {
            try {
                std::rethrow_exception(error);
            } catch (std::exception const &e) {
                std::cerr << e.what() << '\n';
                std::exit(1);
            }
        }
    }

    void handle_event(flimevt::frame_histogram_event<T> const &) {
        std::cerr << "Frame " << (frameCount++) << '\n';
    }

    void
    handle_event(flimevt::final_cumulative_histogram_event<T> const &event) {
        if (!frameCount) { // No frames
            std::cerr << "No frames\n";
            return;
        }

        std::fstream output(outFilename,
                            std::fstream::binary | std::fstream::out);
        if (!output.is_open()) {
            std::cerr << "Cannot open " << outFilename << '\n';
            std::exit(1);
        }

        auto &histogram = event.histogram;
        output.write(reinterpret_cast<const char *>(histogram.get()),
                     histogram.get_number_of_elements() * sizeof(T));
    }
};

int main(int argc, char *argv[]) {
    if (argc != 7) {
        Usage();
        return 1;
    }

    std::uint32_t width = 0;
    std::istringstream(argv[1]) >> width;
    std::uint32_t height = 0;
    std::istringstream(argv[2]) >> height;
    std::uint32_t lineDelay = 0;
    std::istringstream(argv[3]) >> lineDelay;
    std::uint32_t lineTime = 0;
    std::istringstream(argv[4]) >> lineTime;
    std::string inFilename(argv[5]);
    std::string outFilename(argv[6]);

    std::uint32_t maxFrames = UINT32_MAX;

    using SampleType = std::uint16_t;
    std::int32_t inputBits = 12;
    std::int32_t histoBits = 8;
    flimevt::legacy_histogram<SampleType> frameHisto(histoBits, inputBits,
                                                     true, width, height);
    flimevt::legacy_histogram<SampleType> cumulHisto(histoBits, inputBits,
                                                     true, width, height);
    cumulHisto.clear();

    // Construct pipeline
    flimevt::decode_bh_spc decoder(flimevt::line_clock_pixellator(
        width, height, maxFrames, lineDelay, lineTime, 1,
        flimevt::sequential_histogrammer(
            std::move(frameHisto),
            flimevt::histogram_accumulator(
                std::move(cumulHisto),
                HistogramSaver<SampleType>(outFilename)))));

    flimevt::unbatch<std::vector<flimevt::bh_spc_event>, flimevt::bh_spc_event,
                     decltype(decoder)>
        demux(std::move(decoder));

    flimevt::dereference_pointer<
        std::shared_ptr<std::vector<flimevt::bh_spc_event>>, decltype(demux)>
        processor(std::move(demux));

    flimevt::buffer_event<std::shared_ptr<std::vector<flimevt::bh_spc_event>>,
                          decltype(processor)>
        buffer(std::move(processor));

    std::thread procThread([&] { buffer.pump_downstream(); });

    std::fstream input(inFilename, std::fstream::binary | std::fstream::in);
    if (!input.is_open()) {
        std::cerr << "Cannot open " << inFilename << '\n';
        return 1;
    }

    flimevt::object_pool<std::vector<flimevt::bh_spc_event>> pool(2);

    constexpr std::size_t batchCapacity = 48 * 1024;
    constexpr auto maxSize = batchCapacity * sizeof(flimevt::bh_spc_event);

    input.seekg(sizeof(BHSPCFileHeader));
    while (input.good()) {
        auto arr = pool.check_out();
        arr->resize(batchCapacity);
        input.read(reinterpret_cast<char *>(arr->data()), maxSize);
        auto const readSize = input.gcount() / sizeof(flimevt::bh_spc_event);
        arr->resize(static_cast<std::size_t>(readSize));
        buffer.handle_event(arr);
    }
    buffer.handle_end({});

    procThread.join();
    return 0;
}
