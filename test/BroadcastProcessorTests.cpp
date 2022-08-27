#include "FLIMEvents/BroadcastProcessor.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/NoopProcessor.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(HandlesEventSetV<BroadcastProcessor<>, EventSet<>>);

static_assert(HandlesEventSetV<BroadcastProcessor<NoopProcessor<EventSet<>>>,
                               EventSet<>>);

static_assert(
    HandlesEventSetV<BroadcastProcessor<NoopProcessor<EventSet<Event<0>>>>,
                     EventSet<Event<0>>>);

static_assert(
    HandlesEventSetV<BroadcastProcessor<NoopProcessor<EventSet<Event<0>>>,
                                        NoopProcessor<EventSet<Event<0>>>>,
                     EventSet<Event<0>>>);

static_assert(HandlesEventSetV<
              BroadcastProcessor<NoopProcessor<EventSet<Event<0>>>,
                                 NoopProcessor<EventSet<Event<0>, Event<1>>>>,
              EventSet<Event<0>>>);
