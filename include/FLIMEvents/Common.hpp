#pragma once

#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace flimevt {

/**
 * \brief Signed 64-bit integer type representing macrotime.
 *
 * The macrotime is the monotonically increasing timestamp assigned to events
 * by time tagging hardware, after processing to eliminate wraparounds.
 *
 * We used a signed integer type because negative times can arise (for example
 * if a negative delay is applied to events).
 *
 * The physical units of the macrotime is dependent on the input data and it is
 * the user's responsibility to interpret correctly. FLIMEvents is designed to
 * use integer values without scaling and does not handle physical units.
 *
 * It is assumed that macrotime values never overflow. The maximum
 * representable value is over 9E18. If the macrotime units are picoseconds,
 * this corresponds to about 3 and a half months.
 */
using Macrotime = std::int64_t;

namespace internal {

inline int CountTrailingZeros32Nonintrinsic(std::uint32_t x) {
    int r = 0;
    while ((x & 1) == 0) {
        x >>= 1;
        ++r;
    }
    return r;
}

// Return the number of trailing zero bits in x. Behavior is undefined if x is
// zero.
// TODO: In C++20, replace with std::countr_zero()
inline int CountTrailingZeros32(std::uint32_t const x) {
#ifdef __GNUC__
    return __builtin_ctz(x);
#elif defined(_MSC_VER)
    unsigned long r;
    _BitScanForward(&r, x);
    return (int)r;
#else
    return CountTrailingZeros32Nonintrinsic(x);
#endif
}

} // namespace internal

} // namespace flimevt
