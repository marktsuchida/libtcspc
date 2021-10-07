#include "FLIMEvents/DynamicPolymorphism.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/NoopProcessor.hpp"

using namespace flimevt;

struct MyEvent1 {};
struct MyEvent2 {};

static_assert(HandlesEventSetV<PolymorphicProcessor<EventSet<>>, EventSet<>>);
static_assert(
    !HandlesEventSetV<PolymorphicProcessor<EventSet<>>, EventSet<MyEvent1>>);
static_assert(HandlesEventSetV<PolymorphicProcessor<EventSet<MyEvent1>>,
                               EventSet<MyEvent1>>);
static_assert(
    HandlesEventSetV<PolymorphicProcessor<EventSet<MyEvent1, MyEvent2>>,
                     EventSet<MyEvent1, MyEvent2>>);

// HandlesEventSetV works even if the functions are virtual.
static_assert(HandlesEventSetV<VirtualProcessor<EventSet<>>, EventSet<>>);
static_assert(
    !HandlesEventSetV<VirtualProcessor<EventSet<>>, EventSet<MyEvent1>>);
static_assert(HandlesEventSetV<VirtualProcessor<EventSet<MyEvent1>>,
                               EventSet<MyEvent1>>);
static_assert(HandlesEventSetV<VirtualProcessor<EventSet<MyEvent1, MyEvent2>>,
                               EventSet<MyEvent1, MyEvent2>>);

static_assert(HandlesEventSetV<
              VirtualWrappedProcessor<NoopProcessor<EventSet<>>, EventSet<>>,
              EventSet<>>);
static_assert(!HandlesEventSetV<
              VirtualWrappedProcessor<NoopProcessor<EventSet<>>, EventSet<>>,
              EventSet<MyEvent1>>);
static_assert(
    HandlesEventSetV<VirtualWrappedProcessor<NoopProcessor<EventSet<MyEvent1>>,
                                             EventSet<MyEvent1>>,
                     EventSet<MyEvent1>>);
static_assert(
    HandlesEventSetV<
        VirtualWrappedProcessor<NoopProcessor<EventSet<MyEvent1, MyEvent2>>,
                                EventSet<MyEvent1, MyEvent2>>,
        EventSet<MyEvent1, MyEvent2>>);
