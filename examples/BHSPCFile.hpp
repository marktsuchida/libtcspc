/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

// Note: The Becker & Hickl SPCM DLL has SPC_get_fifo_init_vars(), which should
// be used when creating a file header for a measurement.

// Note: Code reading .spc files must know a priori the format: there is no way
// to determine based on file contents whether it is standard format or
// SPC-600/630 (4- or 6-byte) format. (Although it is probably possible to
// guess accurately based on macrotime monotonicity.)

struct bh_spc_file_header {
    std::array<std::uint8_t, 4> bytes;

    void clear() noexcept { memset(bytes.data(), 0, bytes.size()); }

    [[nodiscard]] auto get_macrotime_units_tenths_ns() const noexcept
        -> std::uint32_t {
        return bytes[0] | (std::uint32_t(bytes[1]) << 8) |
               (std::uint32_t(bytes[2]) << 16);
    }

    void set_macrotime_units_tenths_ns(std::uint32_t value) {
        if (value >= 1 << 24) {
            throw std::out_of_range("Macrotime units out of range");
        }
        bytes[0] = value & 0xff;
        bytes[1] = (value >> 8) & 0xff;
        bytes[2] = (value >> 16) & 0xff;
    }

    [[nodiscard]] auto get_number_of_routing_bits() const noexcept
        -> std::uint8_t {
        return (bytes[3] >> 3) & 0x0f;
    }

    void set_number_of_routing_bits(std::uint8_t value) {
        if (value >= 1 << 4) {
            throw std::out_of_range("Number of routing bits out of range");
        }
        bytes[3] &= ~(0x0f << 3);
        bytes[3] |= value << 3;
    }

    [[nodiscard]] auto get_data_valid_flag() const noexcept -> bool {
        return (bytes[3] & (1 << 7)) != 0;
    }

    void set_data_valid_flag(bool valid) {
        if (valid) {
            bytes[3] |= 1 << 7;
        } else {
            bytes[3] &= ~(1 << 7);
        }
    }
};

// SPC-600/630 FIFO_32 file header happens to be identical to the standard FIFO
// file header.
using bh_spc_600_file_header_32 = bh_spc_file_header;

struct bh_spc_600_file_header_48 {
    std::array<std::uint8_t, 6> bytes;

    void clear() noexcept { memset(bytes.data(), 0, bytes.size()); }

    [[nodiscard]] auto get_macrotime_units_tenths_ns() const noexcept
        -> std::uint32_t {
        return bytes[2] | (std::uint32_t(bytes[3]) << 8);
    }

    void set_macrotime_units_tenths_ns(std::uint32_t value) {
        if (value >= 1 << 16) {
            throw std::out_of_range("Macrotime units out of range");
        }
        bytes[2] = value & 0xff;
        bytes[3] = (value >> 8) & 0xff;
    }

    [[nodiscard]] auto get_number_of_routing_bits() const noexcept
        -> std::uint8_t {
        return bytes[1] & 0x0f;
    }

    void set_number_of_routing_bits(std::uint8_t value) {
        if (value >= 1 << 4) {
            throw std::out_of_range("Number of routing bits out of range");
        }
        bytes[1] &= ~0x0f;
        bytes[1] |= value;
    }

    [[nodiscard]] auto get_data_valid_flag() const noexcept -> bool {
        return (bytes[1] & (1 << 4)) != 0;
    }

    void set_data_valid_flag(bool valid) {
        if (valid) {
            bytes[1] |= 1 << 4;
        } else {
            bytes[1] &= ~(1 << 4);
        }
    }
};
