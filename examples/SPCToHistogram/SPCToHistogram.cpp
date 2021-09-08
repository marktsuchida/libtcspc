#include "FLIMEvents/BHDeviceEvent.hpp"
#include "FLIMEvents/Histogram.hpp"
#include "FLIMEvents/LineClockPixellator.hpp"
#include "FLIMEvents/StreamBuffer.hpp"
#include "../BHSPCFile.hpp"

#include <ctime>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>


void Usage() {
    std::cerr <<
        "Test driver for histogramming.\n" <<
        "Usage: SPCToHistogram <width> <height> <lineDelay> <lineTime> input.spc output.raw\n" <<
        "where <lineDelay> and <lineTime> are in macro-time units.\n" <<
        "Currently the output contains only the raw cumulative histogram.\n";
}


template <typename T>
class HistogramSaver : public HistogramProcessor<T> {
    std::size_t frameCount;
    std::string const& outFilename;

public:
    explicit HistogramSaver(std::string const& outFilename) :
        frameCount(0),
        outFilename(outFilename)
    {}

    void HandleError(std::string const& message) override {
        std::cerr << message << '\n';
        std::exit(1);
    }

    void HandleFrame(Histogram<T> const& histogram) override {
        std::cerr << "Frame " << (frameCount++) << '\n';
    }

    void HandleFinish(Histogram<T>&& histogram, bool isCompleteFrame) override {
        if (!frameCount) { // No frames
            std::cerr << "No frames\n";
            return;
        }

        std::fstream output(outFilename, std::fstream::binary | std::fstream::out);
        if (!output.is_open()) {
            std::cerr << "Cannot open " << outFilename << '\n';
            std::exit(1);
        }

        output.write(reinterpret_cast<const char*>(histogram.Get()),
            histogram.GetNumberOfElements() * sizeof(T));
    }
};


int main(int argc, char* argv[])
{
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

    auto processor =
        std::make_shared<LineClockPixellator>(width, height, maxFrames, lineDelay, lineTime, 1,
            std::make_shared<Histogrammer<SampleType>>(std::move(frameHisto),
                std::make_shared<HistogramAccumulator<SampleType>>(std::move(cumulHisto),
                    std::make_shared<HistogramSaver<SampleType>>(outFilename))));

    auto decoder = std::make_shared<BHSPCEventDecoder>(processor);

    EventStream<BHSPCEvent> stream;
    auto processorDone = std::async(std::launch::async, [&stream, decoder]{
        std::clock_t start = std::clock();
        for (;;) {
            auto eventBuffer = stream.ReceiveBlocking();
            if (!eventBuffer) {
                break;
            }
            decoder->HandleDeviceEvents(
                reinterpret_cast<char const*>(eventBuffer->GetData()),
                eventBuffer->GetSize());
        }
        std::clock_t elapsed = std::clock() - start;

        std::cerr << "Approx histogram CPU time: " <<
            1000.0 * elapsed / CLOCKS_PER_SEC << " ms\n";

        decoder->HandleFinish();
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
        input.read(reinterpret_cast<char*>(buf->GetData()), maxSize);
        auto const readSize = input.gcount() / sizeof(BHSPCEvent);
        buf->SetSize(static_cast<std::size_t>(readSize));
        stream.Send(buf);
    }
    stream.Send({});

    processorDone.get();
    return 0;
}
