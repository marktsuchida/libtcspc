#include "../BHSPCFile.hpp"

#include "FLIMEvents/BHDeviceEvents.hpp"
#include "FLIMEvents/Common.hpp"
#include "FLIMEvents/TCSPCEvents.hpp"

#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

class PrintProcessor {
    std::uint32_t count;
    flimevt::Macrotime lastMacrotime;
    std::ostream &output;

    void PrintMacrotime(std::ostream &output, flimevt::Macrotime macrotime) {
        output << std::setw(6) << (count++) << ' ';
        output << std::setw(20) << macrotime;
        if (lastMacrotime > 0) {
            auto delta = macrotime - lastMacrotime;
            output << " (+" << std::setw(16) << delta << ")";
        } else {
            output << "                    ";
        }
        lastMacrotime = macrotime;
    }

  public:
    explicit PrintProcessor(std::ostream &output)
        : count(0), lastMacrotime(0), output(output) {}

    void HandleEvent(flimevt::TimeReachedEvent const &event) {
        // Do nothing
        (void)event;
    }

    void HandleEvent(flimevt::DataLostEvent const &event) {
        PrintMacrotime(output, event.macrotime);
        output << " Data lost\n";
    }

    void HandleEvent(flimevt::TimeCorrelatedCountEvent const &event) {
        PrintMacrotime(output, event.macrotime);
        output << " Photon: " << std::setw(5) << event.nanotime << "; "
               << int(event.channel) << '\n';
    }

    void HandleEvent(flimevt::MarkerEvent const &event) {
        PrintMacrotime(output, event.macrotime);
        output << ' ' << "Marker: " << int(event.channel) << '\n';
    }

    void HandleEnd(std::exception_ptr error) {
        if (error) {
            try {
                std::rethrow_exception(error);
            } catch (std::exception const &e) {
                std::cerr << "Invalid data: " << e.what() << '\n';
                std::exit(1);
            }
        }
    }
};

int DumpHeader(std::istream &input, std::ostream &output) {
    char bytes[sizeof(BHSPCFileHeader)];
    input.read(bytes, sizeof(bytes));
    size_t const bytesRead = input.gcount();
    if (bytesRead < sizeof(bytes)) {
        std::cerr << "File is shorter than required header size\n";
        return 1;
    }

    BHSPCFileHeader header;
    std::memcpy(&header, bytes, sizeof(header));
    output << "Macrotime units (0.1 ns): " << header.GetMacrotimeUnitsTenthNs()
           << '\n';
    output << "Number of routing bits: "
           << int(header.GetNumberOfRoutingBits()) << '\n';
    output << "Data is valid: " << header.GetDataValidFlag() << '\n';

    return 0;
}

void DumpRawEvent(char const *rawEvent, std::ostream &output) {
    flimevt::BHSPCEvent const &event =
        *reinterpret_cast<flimevt::BHSPCEvent const *>(rawEvent);

    std::uint8_t route = event.GetRoutingSignals();
    output << (route & (1 << 3) ? 'x' : '_') << (route & (1 << 2) ? 'x' : '_')
           << (route & (1 << 1) ? 'x' : '_') << (route & (1 << 0) ? 'x' : '_')
           << ' ' << (event.GetInvalidFlag() ? 'I' : '_')
           << (event.GetMacrotimeOverflowFlag() ? 'O' : '_')
           << (event.GetGapFlag() ? 'G' : '_')
           << (event.GetMarkerFlag() ? 'M' : '_');
    if (event.IsMultipleMacrotimeOverflow()) {
        output << ' ' << std::setw(4)
               << event.GetMultipleMacrotimeOverflowCount();
    }
    output << '\n';
}

int DumpEvents(std::istream &input, std::ostream &output) {
    PrintProcessor pp(output);
    flimevt::DecodeBHSPC<decltype(pp)> decoder(std::move(pp));
    constexpr std::size_t const eventSize = sizeof(flimevt::BHSPCEvent);

    while (input.good()) {
        std::vector<char> event(eventSize);
        input.read(event.data(), eventSize);
        auto const bytesRead = input.gcount();
        if (bytesRead == 0) {
            break;
        } else if (static_cast<std::size_t>(bytesRead) < eventSize) {
            std::cerr << bytesRead << " extra bytes at end of file\n";
            return 1;
        }

        DumpRawEvent(event.data(), output);
        decoder.HandleEvent(
            *reinterpret_cast<flimevt::BHSPCEvent *>(event.data()));
    }
    decoder.HandleEnd({});

    return 0;
}

int Dump(std::istream &input, std::ostream &output) {
    int ret = DumpHeader(input, output);
    if (ret)
        return ret;

    ret = DumpEvents(input, output);
    if (ret)
        return ret;

    return 0;
}

int main(int argc, char *argv[]) {

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
