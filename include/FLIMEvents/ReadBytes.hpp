/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>

namespace flimevt::internal {

// Internal functions to read integers from little-endian data streams.

// We provide 2 implementations of each ReadU*LE() function, "Generic" and
// "Memcpy". The "Generic" way is endian-agnostic but is poorly optimized by
// some compilers, including MSVC at the time of writing. The "Memcpy" way is
// only implemented for little-endian targets (for now).

// All variants work with any (lack of) alignment.

// For the "Generic" implementations, see
// https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html
// (but note the need for extra casts for it to actually work correctly).

inline bool IsLittleEndian() {
    // Note: in C++20 this can be replaced with std::endian checks, and be made
    // constexpr.
    union {
        int i;
        char c;
    } t{1};
    return bool(t.c);
}

inline bool UseMemcpy() {
#ifdef _MSC_VER
    return IsLittleEndian();
#else
    return false;
#endif
}

inline std::uint16_t ReadU16LE_Memcpy(unsigned char const *bytes) {
    assert(IsLittleEndian());
    std::uint16_t ret;
    std::memcpy(&ret, bytes, 2);
    return ret;
}

inline std::uint32_t ReadU32LE_Memcpy(unsigned char const *bytes) {
    assert(IsLittleEndian());
    std::uint32_t ret;
    std::memcpy(&ret, bytes, 4);
    return ret;
}

inline std::uint64_t ReadU64LE_Memcpy(unsigned char const *bytes) {
    assert(IsLittleEndian());
    std::uint64_t ret;
    std::memcpy(&ret, bytes, 8);
    return ret;
}

inline std::uint16_t ReadU16LE_Generic(unsigned char const *bytes) {
    // unsigned is at least as wide as uint16_t
    return std::uint16_t(unsigned(bytes[0]) | (unsigned(bytes[1]) << 8));
}

inline std::uint32_t ReadU32LE_Generic(unsigned char const *bytes) {
    using u32 = std::uint32_t;
    if constexpr (sizeof(u32) < sizeof(unsigned)) {
        // u32 would be promoted to int if we don't cast to unsigned
        return u32((unsigned(bytes[0]) << 0) | (unsigned(bytes[1]) << 8) |
                   (unsigned(bytes[2]) << 16) | (unsigned(bytes[3]) << 24));
    } else {
        return (u32(bytes[0]) << 0) | (u32(bytes[1]) << 8) |
               (u32(bytes[2]) << 16) | (u32(bytes[3]) << 24);
    }
}

inline std::uint64_t ReadU64LE_Generic(unsigned char const *bytes) {
    using u64 = std::uint64_t;
    if constexpr (sizeof(u64) < sizeof(unsigned)) {
        // u64 would be promoted to int if we don't cast to unsigned
        return u64((unsigned(bytes[0]) << 0) | (unsigned(bytes[1]) << 8) |
                   (unsigned(bytes[2]) << 16) | (unsigned(bytes[3]) << 24) |
                   (unsigned(bytes[4]) << 32) | (unsigned(bytes[5]) << 40) |
                   (unsigned(bytes[6]) << 48) | (unsigned(bytes[7]) << 56));
    } else {
        return (u64(bytes[0]) << 0) | (u64(bytes[1]) << 8) |
               (u64(bytes[2]) << 16) | (u64(bytes[3]) << 24) |
               (u64(bytes[4]) << 32) | (u64(bytes[5]) << 40) |
               (u64(bytes[6]) << 48) | (u64(bytes[7]) << 56);
    }
}

// For completeness
inline std::uint8_t ReadU8LE(unsigned char const *bytes) { return bytes[0]; }

inline std::uint16_t ReadU16LE(unsigned char const *bytes) {
    if (UseMemcpy())
        return ReadU16LE_Memcpy(bytes);
    else
        return ReadU16LE_Generic(bytes);
}

inline std::uint32_t ReadU32LE(unsigned char const *bytes) {
    if (UseMemcpy())
        return ReadU32LE_Memcpy(bytes);
    else
        return ReadU32LE_Generic(bytes);
}

inline std::uint64_t ReadU64LE(unsigned char const *bytes) {
    if (UseMemcpy())
        return ReadU64LE_Memcpy(bytes);
    else
        return ReadU64LE_Generic(bytes);
}

inline std::int8_t ReadI8LE(unsigned char const *bytes) {
    return std::int8_t(ReadU8LE(bytes));
}

inline std::int16_t ReadI16LE(unsigned char const *bytes) {
    return std::int16_t(ReadU16LE(bytes));
}

inline std::int32_t ReadI32LE(unsigned char const *bytes) {
    return std::int32_t(ReadU32LE(bytes));
}

inline std::int64_t ReadI64LE(unsigned char const *bytes) {
    return std::int64_t(ReadU64LE(bytes));
}

} // namespace flimevt::internal
