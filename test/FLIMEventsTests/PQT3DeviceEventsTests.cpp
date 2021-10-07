#include "FLIMEvents/PQT3DeviceEvents.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/NoopProcessor.hpp"

using namespace flimevt;

using TCSPCNoop = NoopProcessor<TCSPCEvents>;

static_assert(
    HandlesEventSetV<PQPicoT3EventDecoder<TCSPCNoop>, PQPicoT3Events>);
static_assert(
    HandlesEventSetV<PQHydraV1T3EventDecoder<TCSPCNoop>, PQHydraV1T3Events>);
static_assert(
    HandlesEventSetV<PQHydraV2T3EventDecoder<TCSPCNoop>, PQHydraV2T3Events>);
