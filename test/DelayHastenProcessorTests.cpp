/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/DelayHastenProcessor.hpp"

#include "FLIMEvents/Discard.hpp"
#include "FLIMEvents/EventSet.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

static_assert(HandlesEventSetV<
              DelayProcessor<Events01, DiscardAll<Events0123>>, Events0123>);
static_assert(HandlesEventSetV<
              HastenProcessor<Events01, DiscardAll<Events0123>>, Events0123>);
static_assert(HandlesEventSetV<
              DelayHastenProcessor<Events01, Events23, DiscardAll<Events0123>>,
              Events0123>);

// Make fixture to test delaying Events01
auto MakeDelayFixture(Macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return DelayProcessor<Events01, D>(delta, std::move(downstream));
    };

    return MakeProcessorTestFixture<Events0123, Events0123>(makeProc);
}

// Make fixture to test hastening Event01
auto MakeHastenFixture(Macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return HastenProcessor<Events23, D>(delta, std::move(downstream));
    };

    return MakeProcessorTestFixture<Events0123, Events0123>(makeProc);
}

auto MakeDelayHastenFixture(Macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return DelayHastenProcessor<Events01, Events23, D>(
            delta, std::move(downstream));
    };

    return MakeProcessorTestFixture<Events0123, Events0123>(makeProc);
}

using OutVec = std::vector<flimevt::EventVariant<Events0123>>;

TEST_CASE("Delay uniform streams", "[DelayProcessor]") {
    Macrotime delta = GENERATE(0, 1, 2);
    auto f = MakeDelayFixture(delta);

    SECTION("Empty stream yields empty stream") {
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Empty stream with error yields empty stream with error") {
        f.FeedEnd(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f.Output() == OutVec{});
        REQUIRE_THROWS_MATCHES(f.DidEnd(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Undelayed events are unbuffered") {
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                              });
        f.FeedEvents({
            Event<3>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<3>{0},
                              });
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                              });
        f.FeedEvents({
            Event<3>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<3>{0},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Delayed events are buffered") {
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<1>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<1>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{delta},
                                  Event<1>{delta},
                                  Event<0>{delta},
                                  Event<1>{delta},
                              });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Hasten uniform streams", "[HastenProcessor]") {
    Macrotime delta = GENERATE(0, 1, 2);
    auto f = MakeHastenFixture(delta);

    SECTION("Empty stream yields empty stream") {
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Empty stream with error yields empty stream with error") {
        f.FeedEnd(std::make_exception_ptr(std::runtime_error("test")));
        REQUIRE(f.Output() == OutVec{});
        REQUIRE_THROWS_MATCHES(f.DidEnd(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Hastened events are unbuffered") {
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{-delta},
                              });
        f.FeedEvents({
            Event<1>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<1>{-delta},
                              });
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{-delta},
                              });
        f.FeedEvents({
            Event<1>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<1>{-delta},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Unhastened events are buffered") {
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<3>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<3>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                                  Event<3>{0},
                                  Event<2>{0},
                                  Event<3>{0},
                              });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Delay by 0", "[DelayProcessor]") {
    auto f = MakeDelayFixture(0);

    SECTION("Equal timestamps") {
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                                  Event<2>{0},
                              });

        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                                  Event<2>{0},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                                  Event<2>{1},
                              });

        f.FeedEvents({
            Event<0>{2},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{3},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{2},
                                  Event<2>{3},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Hasten by 0", "[HastenProcessor]") {
    auto f = MakeHastenFixture(0);

    SECTION("Equal timestamps") {
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                              });

        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                                  Event<2>{0},
                              });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                                  Event<0>{1},
                              });

        f.FeedEvents({
            Event<2>{2},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{3},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{2},
                                  Event<0>{3},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Delay by 1") {
    auto f = MakeDelayFixture(1);

    SECTION("Equal timestamps") {
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                              });

        f.FeedEvents({
            Event<0>{1},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{1},
                                  Event<2>{1},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{2},
                              });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{1},
                                  Event<2>{1},
                              });

        f.FeedEvents({
            Event<0>{2},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{3},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{3},
                                  Event<2>{3},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Hasten by 1") {
    auto f = MakeHastenFixture(1);

    SECTION("Equal timestamps") {
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{-1},
                              });

        f.FeedEvents({
            Event<2>{1},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                                  Event<2>{1},
                              });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                              });

        f.FeedEvents({
            Event<2>{2},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{3},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                                  Event<0>{2},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{2},
                              });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Delay by 2") {
    auto f = MakeDelayFixture(2);

    SECTION("Equal timestamps") {
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                              });

        f.FeedEvents({
            Event<0>{1},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{1},
                              });

        f.FeedEvents({
            Event<0>{2},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{2},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{2},
                                  Event<2>{2},
                              });

        f.FeedEvents({
            Event<2>{3},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{3},
                                  Event<2>{3},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{4},
                              });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{1},
                              });
        f.FeedEvents({
            Event<0>{2},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{3},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{2},
                                  Event<2>{3},
                              });

        f.FeedEvents({
            Event<0>{4},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<2>{5},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{4},
                                  Event<2>{5},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{6},
                              });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Hasten by 2") {
    auto f = MakeHastenFixture(2);

    SECTION("Equal timestamps") {
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{-2},
                              });

        f.FeedEvents({
            Event<2>{1},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{-1},
                              });

        f.FeedEvents({
            Event<2>{2},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{2},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                              });

        f.FeedEvents({
            Event<0>{3},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                                  Event<0>{1},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{1},
                                  Event<2>{2},
                              });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        f.FeedEvents({
            Event<2>{0},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{-1},
                              });
        f.FeedEvents({
            Event<2>{2},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{3},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{0},
                                  Event<0>{1},
                              });

        f.FeedEvents({
            Event<2>{4},
        });
        REQUIRE(f.Output() == OutVec{});
        f.FeedEvents({
            Event<0>{5},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{2},
                                  Event<0>{3},
                              });

        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{
                                  Event<2>{4},
                              });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("DelayHastenProcessor Sanity", "[DelayHastenProcessor]") {
    Macrotime delta = GENERATE(-2, -1, 0, 1, 2);
    auto f = MakeDelayHastenFixture(delta);

    // Ignore output timing; only check content.

    f.FeedEvents({
        Event<2>{-3},
        Event<0>{0},
        Event<2>{3},
        Event<0>{6},
    });
    OutVec o = f.Output();
    f.FeedEnd({});
    OutVec o1 = f.Output();

    std::copy(o1.cbegin(), o1.cend(), std::back_inserter(o));

    REQUIRE(o == OutVec{
                     Event<2>{-3},
                     Event<0>{0 + delta},
                     Event<2>{3},
                     Event<0>{6 + delta},
                 });

    REQUIRE(f.DidEnd());
}
