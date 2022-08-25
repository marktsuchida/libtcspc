#pragma once

#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace flimevt {

// We used a signed integer to represent macrotime (both duration and
// timepoint), because it is plenty large enough and also allows handling
// negative time.
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
