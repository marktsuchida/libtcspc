/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "../BHSPCFile.hpp"

#include "FLIMEvents/BHDeviceEvents.hpp"
#include "FLIMEvents/Common.hpp"
#include "FLIMEvents/TimeTaggedEvents.hpp"

#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

class PrintProcessor {
    std::uint32_t count;
    flimevt::macrotime lastMacrotime;
    std::ostream &output;

    void PrintMacrotime(std::ostream &output, flimevt::macrotime macrotime) {
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

    void handle_event(flimevt::time_reached_event const &event) {
        // Do nothing
        (void)event;
    }

    void handle_event(flimevt::data_lost_event const &event) {
        PrintMacrotime(output, event.macrotime);
        output << " Data lost\n";
    }

    void handle_event(flimevt::time_correlated_count_event const &event) {
        PrintMacrotime(output, event.macrotime);
        output << " Photon: " << std::setw(5) << event.difftime << "; "
               << int(event.channel) << '\n';
    }

    void handle_event(flimevt::marker_event const &event) {
        PrintMacrotime(output, event.macrotime);
        output << ' ' << "Marker: " << int(event.channel) << '\n';
    }

    void handle_end(std::exception_ptr error) {
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
    flimevt::bh_spc_event const &event =
        *reinterpret_cast<flimevt::bh_spc_event const *>(rawEvent);

    std::uint8_t route = event.get_routing_signals();
    output << (route & (1 << 3) ? 'x' : '_') << (route & (1 << 2) ? 'x' : '_')
           << (route & (1 << 1) ? 'x' : '_') << (route & (1 << 0) ? 'x' : '_')
           << ' ' << (event.get_invalid_flag() ? 'I' : '_')
           << (event.get_macrotime_overflow_flag() ? 'O' : '_')
           << (event.get_gap_flag() ? 'G' : '_')
           << (event.get_marker_flag() ? 'M' : '_');
    if (event.is_multiple_macrotime_overflow()) {
        output << ' ' << std::setw(4)
               << event.get_multiple_macrotime_overflow_count();
    }
    output << '\n';
}

int DumpEvents(std::istream &input, std::ostream &output) {
    PrintProcessor pp(output);
    flimevt::decode_bh_spc<decltype(pp)> decoder(std::move(pp));
    constexpr std::size_t const eventSize = sizeof(flimevt::bh_spc_event);

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
        decoder.handle_event(
            *reinterpret_cast<flimevt::bh_spc_event *>(event.data()));
    }
    decoder.handle_end({});

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
