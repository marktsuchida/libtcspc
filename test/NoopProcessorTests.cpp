#include "FLIMEvents/NoopProcessor.hpp"

#include "FLIMEvents/EventSet.hpp"

using namespace flimevt;

struct MyEvent1 {};
struct MyEvent2 {};

static_assert(HandlesEventSetV<NoopProcessor<EventSet<>>, EventSet<>>);

static_assert(
    HandlesEventSetV<NoopProcessor<EventSet<MyEvent1>>, EventSet<MyEvent1>>);

static_assert(HandlesEventSetV<NoopProcessor<EventSet<MyEvent1, MyEvent2>>,
                               EventSet<MyEvent1, MyEvent2>>);
