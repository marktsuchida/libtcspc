#include "../BHSPCFile.hpp"
#include "FLIMEvents/BHDeviceEvents.hpp"
#include "FLIMEvents/EventBuffer.hpp"
#include "FLIMEvents/Histogram.hpp"
#include "FLIMEvents/LineClockPixellator.hpp"

#include <ctime>
#include <exception>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>

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

    void HandleEnd(std::exception_ptr error) {
        if (error) {
            try {
                std::rethrow_exception(error);
            } catch (std::exception const &e) {
                std::cerr << e.what() << '\n';
                std::exit(1);
            }
        }
    }

    void HandleEvent(flimevt::FrameHistogramEvent<T> const &) {
        std::cerr << "Frame " << (frameCount++) << '\n';
    }

    void HandleEvent(flimevt::FinalCumulativeHistogramEvent<T> const &event) {
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
        output.write(reinterpret_cast<const char *>(histogram.Get()),
                     histogram.GetNumberOfElements() * sizeof(T));
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
    flimevt::Histogram<SampleType> frameHisto(histoBits, inputBits, true,
                                              width, height);
    flimevt::Histogram<SampleType> cumulHisto(histoBits, inputBits, true,
                                              width, height);
    cumulHisto.Clear();

    // Construct pipeline
    flimevt::BHSPCEventDecoder decoder(flimevt::LineClockPixellator(
        width, height, maxFrames, lineDelay, lineTime, 1,
        flimevt::SequentialHistogrammer(
            std::move(frameHisto),
            flimevt::HistogramAccumulator(
                std::move(cumulHisto),
                HistogramSaver<SampleType>(outFilename)))));

    flimevt::EventArrayDemultiplexer<flimevt::BHSPCEvent, decltype(decoder)>
        processor(std::move(decoder));

    flimevt::EventBuffer<decltype(processor)::EventArrayType,
                         decltype(processor)>
        buffer(std::move(processor));

    auto processorDone =
        std::async(std::launch::async, [&] { buffer.PumpDownstream(); });

    std::fstream input(inFilename, std::fstream::binary | std::fstream::in);
    if (!input.is_open()) {
        std::cerr << "Cannot open " << inFilename << '\n';
        return 1;
    }

    flimevt::EventArrayPool<flimevt::BHSPCEvent> pool(48 * 1024, 2);

    input.seekg(sizeof(BHSPCFileHeader));
    while (input.good()) {
        auto arr = pool.CheckOut();
        auto const maxSize = arr->GetCapacity() * sizeof(flimevt::BHSPCEvent);
        input.read(reinterpret_cast<char *>(arr->GetData()), maxSize);
        auto const readSize = input.gcount() / sizeof(flimevt::BHSPCEvent);
        arr->SetSize(static_cast<std::size_t>(readSize));
        buffer.HandleEvent(arr);
    }
    buffer.HandleEnd({});

    processorDone.get();
    return 0;
}
