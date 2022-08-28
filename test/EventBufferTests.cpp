#include "FLIMEvents/EventBuffer.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(
    HandlesEventSetV<EventBuffer<Event<0>, DiscardAll<EventSet<Event<0>>>>,
                     EventSet<Event<0>>>);
