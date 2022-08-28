#include "FLIMEvents/DynamicPolymorphism.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"
#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(HandlesEventSetV<PolymorphicProcessor<EventSet<>>, EventSet<>>);
static_assert(
    !HandlesEventSetV<PolymorphicProcessor<EventSet<>>, EventSet<Event<0>>>);
static_assert(HandlesEventSetV<PolymorphicProcessor<EventSet<Event<0>>>,
                               EventSet<Event<0>>>);
static_assert(HandlesEventSetV<PolymorphicProcessor<Events01>, Events01>);

// HandlesEventSetV works even if the functions are virtual.
static_assert(HandlesEventSetV<VirtualProcessor<EventSet<>>, EventSet<>>);
static_assert(
    !HandlesEventSetV<VirtualProcessor<EventSet<>>, EventSet<Event<0>>>);
static_assert(HandlesEventSetV<VirtualProcessor<EventSet<Event<0>>>,
                               EventSet<Event<0>>>);
static_assert(HandlesEventSetV<VirtualProcessor<Events01>, Events01>);

static_assert(HandlesEventSetV<
              VirtualWrappedProcessor<DiscardAll<EventSet<>>, EventSet<>>,
              EventSet<>>);
static_assert(!HandlesEventSetV<
              VirtualWrappedProcessor<DiscardAll<EventSet<>>, EventSet<>>,
              EventSet<Event<0>>>);
static_assert(
    HandlesEventSetV<VirtualWrappedProcessor<DiscardAll<EventSet<Event<0>>>,
                                             EventSet<Event<0>>>,
                     EventSet<Event<0>>>);
static_assert(
    HandlesEventSetV<VirtualWrappedProcessor<DiscardAll<Events01>, Events01>,
                     Events01>);
