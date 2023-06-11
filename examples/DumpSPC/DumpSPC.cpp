/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "../BHSPCFile.hpp"

#include "flimevt/bh_spc.hpp"
#include "flimevt/common.hpp"
#include "flimevt/time_tagged_events.hpp"

#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

class print_processor {
    std::uint32_t count;
    flimevt::macrotime last_macrotime;
    std::ostream &output;

    void print_macrotime(std::ostream &output, flimevt::macrotime macrotime) {
        output << std::setw(6) << (count++) << ' ';
        output << std::setw(20) << macrotime;
        if (last_macrotime > 0) {
            auto delta = macrotime - last_macrotime;
            output << " (+" << std::setw(16) << delta << ")";
        } else {
            output << "                    ";
        }
        last_macrotime = macrotime;
    }

  public:
    explicit print_processor(std::ostream &output)
        : count(0), last_macrotime(0), output(output) {}

    void handle_event(flimevt::time_reached_event const &event) {
        // Do nothing
        (void)event;
    }

    void handle_event(flimevt::data_lost_event const &event) {
        print_macrotime(output, event.macrotime);
        output << " Data lost\n";
    }

    void handle_event(flimevt::time_correlated_count_event const &event) {
        print_macrotime(output, event.macrotime);
        output << " Photon: " << std::setw(5) << event.difftime << "; "
               << int(event.channel) << '\n';
    }

    void handle_event(flimevt::marker_event const &event) {
        print_macrotime(output, event.macrotime);
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

int dump_header(std::istream &input, std::ostream &output) {
    char bytes[sizeof(bh_spc_file_header)];
    input.read(bytes, sizeof(bytes));
    size_t const bytes_read = static_cast<std::size_t>(input.gcount());
    if (bytes_read < sizeof(bytes)) {
        std::cerr << "File is shorter than required header size\n";
        return 1;
    }

    bh_spc_file_header header{};
    std::memcpy(&header, bytes, sizeof(header));
    output << "Macrotime units (0.1 ns): "
           << header.get_macrotime_units_tenths_ns() << '\n';
    output << "Number of routing bits: "
           << int(header.get_number_of_routing_bits()) << '\n';
    output << "Data is valid: " << header.get_data_valid_flag() << '\n';

    return 0;
}

void dump_raw_event(char const *raw_event, std::ostream &output) {
    flimevt::bh_spc_event const &event =
        *reinterpret_cast<flimevt::bh_spc_event const *>(raw_event);

    std::uint8_t const route = event.get_routing_signals();
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

int dump_events(std::istream &input, std::ostream &output) {
    auto decoder = flimevt::decode_bh_spc(print_processor(output));
    constexpr std::size_t const eventSize = sizeof(flimevt::bh_spc_event);

    while (input.good()) {
        std::vector<char> event(eventSize);
        input.read(event.data(), eventSize);
        auto const bytes_read = input.gcount();
        if (bytes_read == 0) {
            break;
        } else if (static_cast<std::size_t>(bytes_read) < eventSize) {
            std::cerr << bytes_read << " extra bytes at end of file\n";
            return 1;
        }

        dump_raw_event(event.data(), output);
        decoder.handle_event(
            *reinterpret_cast<flimevt::bh_spc_event *>(event.data()));
    }
    decoder.handle_end({});

    return 0;
}

int dump(std::istream &input, std::ostream &output) {
    int ret = dump_header(input, output);
    if (ret)
        return ret;

    ret = dump_events(input, output);
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
        return dump(std::cin, std::cout);
    }

    auto filename = argv[1];
    std::fstream input(filename, std::fstream::binary | std::fstream::in);
    if (!input.is_open()) {
        std::cerr << "Cannot open " << filename << '\n';
        return 1;
    }

    return dump(input, std::cout);
}
