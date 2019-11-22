#pragma once

#include <cstdint>
#include <stdexcept>


// Note: The Becker & Hickl SPCM DLL has SPC_get_fifo_init_vars(), which should
// be used when creating a file header for a measurement.

// Note: Code reading .spc files must know a priori the format: there is no way
// to determine based on file contents whether it is standard format or
// SPC-600/630 (4- or 6-byte) format. (Although it is probably possible to
// guess accurately based on macro-time monotonicity.)


struct BHSPCFileHeader {
    uint8_t bytes[4];

    void Clear() noexcept {
        memset(bytes, 0, sizeof(bytes));
    }

    uint32_t GetMacroTimeUnitsTenthNs() const noexcept {
        return bytes[0] | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16);
    }

    void SetMacroTimeUnitsTenthNs(uint32_t value) {
        if (value >= 1 << 24) {
            throw std::out_of_range("Macro-time units out of range");
        }
        bytes[0] = value & 0xff;
        bytes[1] = (value >> 8) & 0xff;
        bytes[2] = (value >> 16) & 0xff;
    }

    uint8_t GetNumberOfRoutingBits() const noexcept {
        return (bytes[3] >> 3) & 0x0f;
    }

    void SetNumberOfRoutingBits(uint8_t value) {
        if (value >= 1 << 4) {
            throw std::out_of_range("Number of routing bits out of range");
        }
        bytes[3] &= ~(0x0f << 3);
        bytes[3] |= value << 3;
    }

    bool GetDataValidFlag() const noexcept {
        return bytes[3] & (1 << 7);
    }

    void SetDataValidFlag(bool valid) {
        if (valid) {
            bytes[3] |= 1 << 7;
        }
        else {
            bytes[3] &= ~(1 << 7);
        }
    }
};


// SPC-600/630 FIFO_32 file header happens to be identical to the standard FIFO
// file header.
using BHSPC600FileHeader32 = BHSPCFileHeader;


struct BHSPC600FileHeader48 {
    uint8_t bytes[6];

    void Clear() noexcept {
        memset(bytes, 0, sizeof(bytes));
    }

    uint32_t GetMacroTimeUnitsTenthNs() const noexcept {
        return bytes[2] | (uint32_t(bytes[3]) << 8);
    }

    void SetMacroTimeUnitsTenthsNs(uint32_t value) {
        if (value >= 1 << 16) {
            throw std::out_of_range("Macro-time units out of range");
        }
        bytes[2] = value & 0xff;
        bytes[3] = (value >> 8) & 0xff;
    }

    uint8_t GetNumberOfRoutingBits() const noexcept {
        return bytes[1] & 0x0f;
    }

    void SetNumberOfRoutingBits(uint8_t value) {
        if (value >= 1 << 4) {
            throw std::out_of_range("Number of routing bits out of range");
        }
        bytes[1] &= ~0x0f;
        bytes[1] |= value;
    }

    bool GetDataValidFlag() const noexcept {
        return bytes[1] & (1 << 4);
    }

    void SetDataValidFlag(bool valid) {
        if (valid) {
            bytes[1] |= 1 << 4;
        }
        else {
            bytes[1] &= ~(1 << 4);
        }
    }
};
