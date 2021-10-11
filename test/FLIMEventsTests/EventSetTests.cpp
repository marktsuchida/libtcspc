#include "FLIMEvents/EventSet.hpp"

using namespace flimevt;

struct MyEvent1 {};
struct MyEvent2 {};

static_assert(std::is_same_v<EventVariant<EventSet<>>, std::variant<>>);
static_assert(std::is_same_v<EventVariant<EventSet<MyEvent1, MyEvent2>>,
                             std::variant<MyEvent1, MyEvent2>>);

static_assert(!ContainsEventV<EventSet<>, MyEvent1>);
static_assert(ContainsEventV<EventSet<MyEvent1>, MyEvent1>);
static_assert(!ContainsEventV<EventSet<MyEvent2>, MyEvent1>);
static_assert(ContainsEventV<EventSet<MyEvent1, MyEvent2>, MyEvent1>);
static_assert(ContainsEventV<EventSet<MyEvent1, MyEvent2>, MyEvent2>);

struct MyEvent1Processor {
    void HandleEvent(MyEvent1 const &) noexcept {}
};

static_assert(HandlesEventV<MyEvent1Processor, MyEvent1>);
static_assert(!HandlesEventV<MyEvent1Processor, MyEvent2>);
static_assert(!HandlesEndV<MyEvent1Processor>);
static_assert(!HandlesEventSetV<MyEvent1Processor, EventSet<MyEvent1>>);

struct MyEndProcessor {
    void HandleEnd(std::exception_ptr) noexcept {}
};

static_assert(!HandlesEventV<MyEndProcessor, MyEvent1>);
static_assert(HandlesEndV<MyEndProcessor>);
static_assert(HandlesEventSetV<MyEndProcessor, EventSet<>>);

struct MyEvent1SetProcessor : MyEvent1Processor, MyEndProcessor {};

static_assert(HandlesEventV<MyEvent1SetProcessor, MyEvent1>);
static_assert(HandlesEndV<MyEvent1SetProcessor>);
static_assert(HandlesEventSetV<MyEvent1SetProcessor, EventSet<MyEvent1>>);
static_assert(!HandlesEventSetV<MyEvent1SetProcessor, EventSet<MyEvent2>>);
static_assert(
    !HandlesEventSetV<MyEvent1SetProcessor, EventSet<MyEvent1, MyEvent2>>);
