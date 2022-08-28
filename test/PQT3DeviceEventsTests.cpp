#include "FLIMEvents/PQT3DeviceEvents.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"

using namespace flimevt;

using DiscardTCSPC = DiscardAll<TCSPCEvents>;

static_assert(HandlesEventSetV<DecodePQPicoT3<DiscardTCSPC>, PQPicoT3Events>);
static_assert(
    HandlesEventSetV<DecodePQHydraV1T3<DiscardTCSPC>, PQHydraV1T3Events>);
static_assert(
    HandlesEventSetV<DecodePQHydraV2T3<DiscardTCSPC>, PQHydraV2T3Events>);
