#include "FLIMEvents/Broadcast.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(HandlesEventSetV<Broadcast<>, EventSet<>>);

static_assert(HandlesEventSetV<Broadcast<DiscardAll<EventSet<>>>, EventSet<>>);

static_assert(HandlesEventSetV<Broadcast<DiscardAll<EventSet<Event<0>>>>,
                               EventSet<Event<0>>>);

static_assert(HandlesEventSetV<Broadcast<DiscardAll<EventSet<Event<0>>>,
                                         DiscardAll<EventSet<Event<0>>>>,
                               EventSet<Event<0>>>);

static_assert(
    HandlesEventSetV<Broadcast<DiscardAll<EventSet<Event<0>>>,
                               DiscardAll<EventSet<Event<0>, Event<1>>>>,
                     EventSet<Event<0>>>);
