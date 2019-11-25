#include "FLIMEvents/BHDeviceEvent.hpp"
#include "../BHSPCFile.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>


class PrintProcessor : public DecodedEventProcessor {
    uint32_t count;
    uint64_t lastMacrotime;
    std::ostream& output;

    void PrintMacroTime(std::ostream& output, uint64_t macrotime) {
        output << std::setw(6) << (count++) << ' ';
        output << std::setw(20) << macrotime;
        if (lastMacrotime > 0) {
            uint64_t delta = macrotime - lastMacrotime;
            output << " (+" << std::setw(16) << delta << ")";
        }
        else {
            output << "                    ";
        }
        lastMacrotime = macrotime;
    }

public:
    explicit PrintProcessor(std::ostream& output) :
        count(0), lastMacrotime(0), output(output)
    {}

    void HandleTimestamp(DecodedEvent const& event) override {
        // Do nothing
    }

    void HandleDataLost(DataLostEvent const& event) override {
        PrintMacroTime(output, event.macrotime);
        output << " Data lost\n";
    }

    void HandleValidPhoton(ValidPhotonEvent const& event) override {
        PrintMacroTime(output, event.macrotime);
        output << " Photon: " <<
            std::setw(5) << event.microtime << "; " <<
            int(event.route) << '\n';
    }

    void HandleInvalidPhoton(InvalidPhotonEvent const& event) override {
        PrintMacroTime(output, event.macrotime);
        output << " Invalid photon: " <<
            std::setw(5) << event.microtime << "; " <<
            int(event.route) << '\n';
    }

    void HandleMarker(MarkerEvent const& event) override {
        PrintMacroTime(output, event.macrotime);
        output << ' ' << "Marker: " << int(event.bits) << '\n';
    }

    void HandleError(std::string const& message) override {
        std::cerr << "Invalid data: " << message << '\n';
        std::exit(1);
    }

    void HandleFinish() override {
        // Ignore
    }
};


int DumpHeader(std::istream& input, std::ostream& output)
{
    union {
        BHSPCFileHeader header;
        char bytes[sizeof(BHSPCFileHeader)];
    } data;

    input.read(data.bytes, sizeof(data));
    auto const bytesRead = input.gcount();
    if (bytesRead < sizeof(data)) {
        std::cerr << "File is shorter than required header size\n";
        return 1;
    }

    BHSPCFileHeader const& header = data.header;
    output << "Macro-time units (0.1 ns): " << header.GetMacroTimeUnitsTenthNs() << '\n';
    output << "Number of routing bits: " << int(header.GetNumberOfRoutingBits()) << '\n';
    output << "Data is valid: " << header.GetDataValidFlag() << '\n';

    return 0;
}


void DumpRawEvent(char const* rawEvent, std::ostream& output)
{
    BHSPCEvent const& event = *reinterpret_cast<BHSPCEvent const*>(rawEvent);

    uint8_t route = event.GetRoutingSignals();
    output <<
        (route & (1 << 3) ? 'x' : '_') <<
        (route & (1 << 2) ? 'x' : '_') <<
        (route & (1 << 1) ? 'x' : '_') <<
        (route & (1 << 0) ? 'x' : '_') <<
        ' ' <<
        (event.GetInvalidFlag() ? 'I' : '_') <<
        (event.GetMacroTimeOverflowFlag() ? 'O' : '_') <<
        (event.GetGapFlag() ? 'G' : '_') <<
        (event.GetMarkerFlag() ? 'M' : '_');
    if (event.IsMultipleMacroTimeOverflow()) {
        output << ' ' << std::setw(4) <<
            event.GetMultipleMacroTimeOverflowCount();
    }
    output << '\n';
}


int DumpEvents(std::istream& input, std::ostream& output)
{
    BHSPCEventDecoder decoder;
    decoder.SetDownstream(std::make_shared<PrintProcessor>(output));
    std::size_t const eventSize = decoder.GetEventSize();

    while (input.good()) {
        std::vector<char> event(eventSize);
        input.read(event.data(), eventSize);
        auto const bytesRead = input.gcount();
        if (bytesRead == 0) {
            break;
        }
        else if (static_cast<std::size_t>(bytesRead) < eventSize) {
            std::cerr << bytesRead << " extra bytes at end of file\n";
            return 1;
        }

        DumpRawEvent(event.data(), output);
        decoder.HandleDeviceEvent(event.data());
    }
    decoder.HandleFinish();

    return 0;
}


int Dump(std::istream& input, std::ostream& output)
{
    int ret = DumpHeader(input, output);
    if (ret)
        return ret;

    ret = DumpEvents(input, output);
    if (ret)
        return ret;

    return 0;
}


int main(int argc, char* argv[])
{

    if (argc > 2) {
        std::cerr << "Too many arguments\n";
        return 1;
    }

    if (argc < 2) {
        return Dump(std::cin, std::cout);
    }

    auto filename = argv[1];
    std::fstream input(filename, std::fstream::binary | std::fstream::in);
    if (!input.is_open()) {
        std::cerr << "Cannot open " << filename << '\n';
        return 1;
    }

    return Dump(input, std::cout);
}
