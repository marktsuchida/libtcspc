#include "../BHSPCFile.hpp"
#include "FLIMEvents/BHDeviceEvent.hpp"
#include "FLIMEvents/Histogram.hpp"
#include "FLIMEvents/LineClockPixellator.hpp"
#include "FLIMEvents/StreamBuffer.hpp"

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
        << "where <lineDelay> and <lineTime> are in macro-time units.\n"
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

    void HandleEvent(FrameHistogramEvent<T> const &) {
        std::cerr << "Frame " << (frameCount++) << '\n';
    }

    void HandleEvent(FinalCumulativeHistogramEvent<T> const &event) {
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

    uint32_t width = 0;
    std::istringstream(argv[1]) >> width;
    uint32_t height = 0;
    std::istringstream(argv[2]) >> height;
    uint32_t lineDelay = 0;
    std::istringstream(argv[3]) >> lineDelay;
    uint32_t lineTime = 0;
    std::istringstream(argv[4]) >> lineTime;
    std::string inFilename(argv[5]);
    std::string outFilename(argv[6]);

    uint32_t maxFrames = UINT32_MAX;

    using SampleType = uint16_t;
    int32_t inputBits = 12;
    int32_t histoBits = 8;
    Histogram<SampleType> frameHisto(histoBits, inputBits, true, width, height);
    Histogram<SampleType> cumulHisto(histoBits, inputBits, true, width, height);
    cumulHisto.Clear();

    // Construct pipeline from tail to head

    HistogramSaver<SampleType> saver(outFilename);

    HistogramAccumulator<SampleType, decltype(saver)> accumulator(
        std::move(cumulHisto), std::move(saver));

    Histogrammer<SampleType, decltype(accumulator)> histogrammer(
        std::move(frameHisto), std::move(accumulator));

    LineClockPixellator<decltype(histogrammer)> pixellator(
        width, height, maxFrames, lineDelay, lineTime, 1,
        std::move(histogrammer));

    BHSPCEventDecoder<decltype(pixellator)> decoder(std::move(pixellator));

    EventStream<BHSPCEvent> stream;
    auto processorDone = std::async(std::launch::async, [&stream, &decoder] {
        std::clock_t start = std::clock();
        for (;;) {
            auto eventBuffer = stream.ReceiveBlocking();
            if (!eventBuffer) {
                break;
            }
            for (std::size_t i = 0; i < eventBuffer->GetSize(); ++i) {
                decoder.HandleEvent(*(eventBuffer->GetData() + i));
            }
        }
        std::clock_t elapsed = std::clock() - start;

        std::cerr << "Approx histogram CPU time: "
                  << 1000.0 * elapsed / CLOCKS_PER_SEC << " ms\n";

        decoder.HandleEnd({});
    });

    std::fstream input(inFilename, std::fstream::binary | std::fstream::in);
    if (!input.is_open()) {
        std::cerr << "Cannot open " << inFilename << '\n';
        return 1;
    }

    EventBufferPool<BHSPCEvent> pool(48 * 1024);

    input.seekg(sizeof(BHSPCFileHeader));
    while (input.good()) {
        auto buf = pool.CheckOut();
        auto const maxSize = buf->GetCapacity() * sizeof(BHSPCEvent);
        input.read(reinterpret_cast<char *>(buf->GetData()), maxSize);
        auto const readSize = input.gcount() / sizeof(BHSPCEvent);
        buf->SetSize(static_cast<std::size_t>(readSize));
        stream.Send(buf);
    }
    stream.Send({});

    processorDone.get();
    return 0;
}
