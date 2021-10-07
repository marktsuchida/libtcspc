#include "FLIMEvents/EventBuffer.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/NoopProcessor.hpp"

using namespace flimevt;

struct MyEvent {};

static_assert(
    HandlesEventSetV<EventBuffer<MyEvent, NoopProcessor<EventSet<MyEvent>>>,
                     EventSet<MyEvent>>);
