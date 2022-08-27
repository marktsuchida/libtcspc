#include "FLIMEvents/EventBuffer.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/NoopProcessor.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(
    HandlesEventSetV<EventBuffer<Event<0>, NoopProcessor<EventSet<Event<0>>>>,
                     EventSet<Event<0>>>);
