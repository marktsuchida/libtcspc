/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/EventSet.hpp"

#include "TestEvents.hpp"

using namespace flimevt;
using namespace flimevt::test;

static_assert(std::is_same_v<EventVariant<EventSet<>>, std::variant<>>);
static_assert(std::is_same_v<EventVariant<EventSet<Event<0>, Event<1>>>,
                             std::variant<Event<0>, Event<1>>>);

static_assert(!ContainsEventV<EventSet<>, Event<0>>);
static_assert(ContainsEventV<EventSet<Event<0>>, Event<0>>);
static_assert(!ContainsEventV<EventSet<Event<1>>, Event<0>>);
static_assert(ContainsEventV<EventSet<Event<0>, Event<1>>, Event<0>>);
static_assert(ContainsEventV<EventSet<Event<0>, Event<1>>, Event<1>>);

struct MyEvent1Processor {
    void HandleEvent(Event<0> const &) noexcept {}
};

static_assert(HandlesEventV<MyEvent1Processor, Event<0>>);
static_assert(!HandlesEventV<MyEvent1Processor, Event<1>>);
static_assert(!HandlesEndV<MyEvent1Processor>);
static_assert(!HandlesEventSetV<MyEvent1Processor, EventSet<Event<0>>>);

struct MyEndProcessor {
    void HandleEnd(std::exception_ptr) noexcept {}
};

static_assert(!HandlesEventV<MyEndProcessor, Event<0>>);
static_assert(HandlesEndV<MyEndProcessor>);
static_assert(HandlesEventSetV<MyEndProcessor, EventSet<>>);

struct MyEvent1SetProcessor : MyEvent1Processor, MyEndProcessor {};

static_assert(HandlesEventV<MyEvent1SetProcessor, Event<0>>);
static_assert(HandlesEndV<MyEvent1SetProcessor>);
static_assert(HandlesEventSetV<MyEvent1SetProcessor, EventSet<Event<0>>>);
static_assert(!HandlesEventSetV<MyEvent1SetProcessor, EventSet<Event<1>>>);
static_assert(
    !HandlesEventSetV<MyEvent1SetProcessor, EventSet<Event<0>, Event<1>>>);

static_assert(
    std::is_same_v<ConcatEventSetT<EventSet<Event<0>>, EventSet<Event<1>>>,
                   EventSet<Event<0>, Event<1>>>);
