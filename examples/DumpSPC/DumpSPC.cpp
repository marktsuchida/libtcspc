/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "../BHSPCFile.hpp"

#include "libtcspc/bh_spc.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/time_tagged_events.hpp"

#include <array>
#include <cstring>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

class print_processor {
    std::uint32_t count = 0;
    tcspc::default_data_traits::abstime_type last_macrotime = 0;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    std::ostream &output;

    void print_macrotime(std::ostream &output,
                         tcspc::default_data_traits::abstime_type macrotime) {
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
    explicit print_processor(std::ostream &output) : output(output) {}

    static void handle_event(tcspc::time_reached_event<> const &event) {
        // Do nothing
        (void)event;
    }

    void handle_event(tcspc::data_lost_event<> const &event) {
        print_macrotime(output, event.macrotime);
        output << " Data lost\n";
    }

    void handle_event(tcspc::time_correlated_detection_event<> const &event) {
        print_macrotime(output, event.macrotime);
        output << " Photon: " << std::setw(5) << event.difftime << "; "
               << int(event.channel) << '\n';
    }

    void handle_event(tcspc::marker_event<> const &event) {
        print_macrotime(output, event.macrotime);
        output << ' ' << "Marker: " << int(event.channel) << '\n';
    }

    static void handle_end(std::exception_ptr const &error) {
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

auto dump_header(std::istream &input, std::ostream &output) -> int {
    std::array<char, sizeof(bh_spc_file_header)> bytes{};
    input.read(bytes.data(), bytes.size());
    auto const bytes_read = static_cast<std::size_t>(input.gcount());
    if (bytes_read < bytes.size()) {
        std::cerr << "File is shorter than required header size\n";
        return 1;
    }

    bh_spc_file_header header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    output << "Macrotime units (0.1 ns): "
           << header.get_macrotime_units_tenths_ns() << '\n';
    output << "Number of routing bits: "
           << int(header.get_number_of_routing_bits()) << '\n';
    output << "Data is valid: " << header.get_data_valid_flag() << '\n';

    return 0;
}

void dump_raw_event(char const *raw_event, std::ostream &output) {
    tcspc::bh_spc_event event{};
    std::memcpy(&event, raw_event, sizeof(event));

    using namespace tcspc::literals;
    auto const route = event.routing_signals();
    output << ((route & (1_u8np << 3)) != 0_u8np ? 'x' : '_')
           << ((route & (1_u8np << 2)) != 0_u8np ? 'x' : '_')
           << ((route & (1_u8np << 1)) != 0_u8np ? 'x' : '_')
           << ((route & (1_u8np << 0)) != 0_u8np ? 'x' : '_') << ' '
           << (event.invalid_flag() ? 'I' : '_')
           << (event.macrotime_overflow_flag() ? 'O' : '_')
           << (event.gap_flag() ? 'G' : '_')
           << (event.marker_flag() ? 'M' : '_');
    if (event.is_multiple_macrotime_overflow()) {
        output << ' ' << std::setw(4)
               << event.multiple_macrotime_overflow_count();
    }
    output << '\n';
}

auto dump_events(std::istream &input, std::ostream &output) -> int {
    auto decoder = tcspc::decode_bh_spc<tcspc::default_data_traits>(
        print_processor(output));
    constexpr std::size_t const eventSize = sizeof(tcspc::bh_spc_event);

    while (input.good()) {
        std::vector<char> event(eventSize);
        input.read(event.data(), eventSize);
        auto const bytes_read = input.gcount();
        if (bytes_read == 0)
            break;
        if (static_cast<std::size_t>(bytes_read) < eventSize) {
            std::cerr << bytes_read << " extra bytes at end of file\n";
            return 1;
        }

        dump_raw_event(event.data(), output);
        tcspc::bh_spc_event e{};
        std::memcpy(&e, event.data(), sizeof(e));
        decoder.handle_event(e);
    }
    decoder.handle_end({});

    return 0;
}

auto dump(std::istream &input, std::ostream &output) -> int {
    int ret = dump_header(input, output);
    if (ret != 0)
        return ret;

    ret = dump_events(input, output);
    if (ret != 0)
        return ret;

    return 0;
}

auto main(int argc, char *argv[]) -> int {
    try {
        if (argc > 2) {
            std::cerr << "Too many arguments\n";
            return 1;
        }

        if (argc < 2) {
            return dump(std::cin, std::cout);
        }

        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        auto *filename = argv[1];
        std::fstream input(filename, std::fstream::binary | std::fstream::in);
        if (!input.is_open()) {
            std::cerr << "Cannot open " << filename << '\n';
            return 1;
        }

        return dump(input, std::cout);
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
