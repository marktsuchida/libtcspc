#include "FLIMEvents/NoopProcessor.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(HandlesEventSetV<NoopProcessor<EventSet<>>, EventSet<>>);
static_assert(HandlesEventSetV<NoopProcessor<Events01>, Events01>);
static_assert(!HandlesEventSetV<NoopProcessor<Events01>, Events23>);
