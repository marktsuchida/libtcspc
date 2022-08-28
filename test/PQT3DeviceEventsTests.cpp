#include "FLIMEvents/PQT3DeviceEvents.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"

using namespace flimevt;

using DiscardTCSPC = DiscardAll<TCSPCEvents>;

static_assert(
    HandlesEventSetV<PQPicoT3EventDecoder<DiscardTCSPC>, PQPicoT3Events>);
static_assert(HandlesEventSetV<PQHydraV1T3EventDecoder<DiscardTCSPC>,
                               PQHydraV1T3Events>);
static_assert(HandlesEventSetV<PQHydraV2T3EventDecoder<DiscardTCSPC>,
                               PQHydraV2T3Events>);
