#include "FLIMEvents/Discard.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(HandlesEventSetV<DiscardAll<EventSet<>>, EventSet<>>);
static_assert(HandlesEventSetV<DiscardAll<Events01>, Events01>);
static_assert(!HandlesEventSetV<DiscardAll<Events01>, Events23>);
