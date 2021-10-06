#pragma once

#include <cstdint>

namespace flimevt {

// We used a signed integer to represent macrotime (both duration and
// timepoint), because it is plenty large enough and also allows handling
// negative time.
using Macrotime = std::int64_t;

} // namespace flimevt
