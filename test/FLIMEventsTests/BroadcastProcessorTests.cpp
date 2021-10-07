#include "FLIMEvents/BroadcastProcessor.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/NoopProcessor.hpp"

using namespace flimevt;

struct MyEvent1 {};
struct MyEvent2 {};

static_assert(HandlesEventSetV<BroadcastProcessor<>, EventSet<>>);

static_assert(HandlesEventSetV<BroadcastProcessor<NoopProcessor<EventSet<>>>,
                               EventSet<>>);

static_assert(
    HandlesEventSetV<BroadcastProcessor<NoopProcessor<EventSet<MyEvent1>>>,
                     EventSet<MyEvent1>>);

static_assert(
    HandlesEventSetV<BroadcastProcessor<NoopProcessor<EventSet<MyEvent1>>,
                                        NoopProcessor<EventSet<MyEvent1>>>,
                     EventSet<MyEvent1>>);

static_assert(HandlesEventSetV<
              BroadcastProcessor<NoopProcessor<EventSet<MyEvent1>>,
                                 NoopProcessor<EventSet<MyEvent1, MyEvent2>>>,
              EventSet<MyEvent1>>);
