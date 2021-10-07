#include "FLIMEvents/DelayHastenProcessor.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "FLIMEvents/NoopProcessor.hpp"
#include "ProcessorTestFixture.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
#include <utility>

struct Event0 {
    flimevt::Macrotime macrotime;
};

struct Event1 {
    flimevt::Macrotime macrotime;
};

struct Event2 {
    flimevt::Macrotime macrotime;
};

struct Event3 {
    flimevt::Macrotime macrotime;
};

using Events01 = flimevt::EventSet<Event0, Event1>;
using Events23 = flimevt::EventSet<Event2, Event3>;
using AllEvents = flimevt::EventSet<Event0, Event1, Event2, Event3>;

template <typename E,
          typename = std::enable_if_t<flimevt::ContainsEventV<AllEvents, E>>>
std::ostream &operator<<(std::ostream &os, E const &e) {
    return os << typeid(e).name() << '{' << e.macrotime << '}';
}

std::ostream &operator<<(std::ostream &os,
                         flimevt::EventVariant<AllEvents> const &event) {
    return std::visit([&](auto const &e) -> std::ostream & { return os << e; },
                      event);
}

// Must include after defining operator<< overloads
#include <catch2/catch.hpp>

using namespace flimevt;

static_assert(HandlesEventSetV<
              DelayProcessor<Events01, NoopProcessor<AllEvents>>, AllEvents>);
static_assert(HandlesEventSetV<
              HastenProcessor<Events01, NoopProcessor<AllEvents>>, AllEvents>);
static_assert(HandlesEventSetV<DelayHastenProcessor<Events01, Events23,
                                                    NoopProcessor<AllEvents>>,
                               AllEvents>);

template <typename E,
          typename = std::enable_if_t<ContainsEventV<AllEvents, E>>>
bool operator==(E const &lhs, E const &rhs) {
    return lhs.macrotime == rhs.macrotime;
}

// Make fixture to test delaying Event0 and Event1
auto MakeDelayFixture(Macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return DelayProcessor<Events01, D>(delta, std::move(downstream));
    };

    return test::MakeProcessorTestFixture<AllEvents, AllEvents>(makeProc);
}

// Make fixture to test hastening Event0 and Event1
auto MakeHastenFixture(Macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return HastenProcessor<Events23, D>(delta, std::move(downstream));
    };

    return test::MakeProcessorTestFixture<AllEvents, AllEvents>(makeProc);
}

auto MakeDelayHastenFixture(Macrotime delta) {
    auto makeProc = [delta](auto &&downstream) {
        using D = std::remove_reference_t<decltype(downstream)>;
        return DelayHastenProcessor<Events01, Events23, D>(
            delta, std::move(downstream));
    };

    return test::MakeProcessorTestFixture<AllEvents, AllEvents>(makeProc);
}

using OutVec = std::vector<flimevt::EventVariant<AllEvents>>;

TEST_CASE("Delay uniform streams", "[DelayProcessor]") {
    Macrotime delta = GENERATE(0, 1, 2);
    auto f = MakeDelayFixture(delta);

    SECTION("Empty stream yields empty stream") {
        REQUIRE(f.FeedEnd({}) == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Empty stream with error yields empty stream with error") {
        REQUIRE(f.FeedEnd(std::make_exception_ptr(
                    std::runtime_error("test"))) == OutVec{});
        REQUIRE_THROWS_MATCHES(f.DidEnd(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Undelayed events are unbuffered") {
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{
                          Event2{0},
                      });
        REQUIRE(f.FeedEvents({
                    Event3{0},
                }) == OutVec{
                          Event3{0},
                      });
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{
                          Event2{0},
                      });
        REQUIRE(f.FeedEvents({
                    Event3{0},
                }) == OutVec{
                          Event3{0},
                      });
        REQUIRE(f.FeedEnd({}) == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Delayed events are buffered") {
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event1{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event1{0},
                }) == OutVec{});
        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event0{delta},
                                     Event1{delta},
                                     Event0{delta},
                                     Event1{delta},
                                 });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Hasten uniform streams", "[HastenProcessor]") {
    Macrotime delta = GENERATE(0, 1, 2);
    auto f = MakeHastenFixture(delta);

    SECTION("Empty stream yields empty stream") {
        REQUIRE(f.FeedEnd({}) == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Empty stream with error yields empty stream with error") {
        REQUIRE(f.FeedEnd(std::make_exception_ptr(
                    std::runtime_error("test"))) == OutVec{});
        REQUIRE_THROWS_MATCHES(f.DidEnd(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Hastened events are unbuffered") {
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{
                          Event0{-delta},
                      });
        REQUIRE(f.FeedEvents({
                    Event1{0},
                }) == OutVec{
                          Event1{-delta},
                      });
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{
                          Event0{-delta},
                      });
        REQUIRE(f.FeedEvents({
                    Event1{0},
                }) == OutVec{
                          Event1{-delta},
                      });
        REQUIRE(f.FeedEnd({}) == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Unhastened events are buffered") {
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event3{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event3{0},
                }) == OutVec{});
        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event2{0},
                                     Event3{0},
                                     Event2{0},
                                     Event3{0},
                                 });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Delay by 0", "[DelayProcessor]") {
    auto f = MakeDelayFixture(0);

    SECTION("Equal timestamps") {
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{
                          Event0{0},
                          Event2{0},
                      });

        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{
                          Event0{0},
                          Event2{0},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{1},
                }) == OutVec{
                          Event0{0},
                          Event2{1},
                      });

        REQUIRE(f.FeedEvents({
                    Event0{2},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{3},
                }) == OutVec{
                          Event0{2},
                          Event2{3},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{});
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Hasten by 0", "[HastenProcessor]") {
    auto f = MakeHastenFixture(0);

    SECTION("Equal timestamps") {
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{
                          Event0{0},
                      });

        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{
                          Event0{0},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event2{0},
                                     Event2{0},
                                 });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{1},
                }) == OutVec{
                          Event2{0},
                          Event0{1},
                      });

        REQUIRE(f.FeedEvents({
                    Event2{2},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{3},
                }) == OutVec{
                          Event2{2},
                          Event0{3},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{});
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Delay by 1") {
    auto f = MakeDelayFixture(1);

    SECTION("Equal timestamps") {
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{
                          Event2{0},
                      });

        REQUIRE(f.FeedEvents({
                    Event0{1},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{1},
                }) == OutVec{
                          Event0{1},
                          Event2{1},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event0{2},
                                 });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{1},
                }) == OutVec{
                          Event0{1},
                          Event2{1},
                      });

        REQUIRE(f.FeedEvents({
                    Event0{2},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{3},
                }) == OutVec{
                          Event0{3},
                          Event2{3},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{});
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Hasten by 1") {
    auto f = MakeHastenFixture(1);

    SECTION("Equal timestamps") {
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{
                          Event0{-1},
                      });

        REQUIRE(f.FeedEvents({
                    Event2{1},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{1},
                }) == OutVec{
                          Event0{0},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event2{0},
                                     Event2{1},
                                 });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{1},
                }) == OutVec{
                          Event0{0},
                      });

        REQUIRE(f.FeedEvents({
                    Event2{2},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{3},
                }) == OutVec{
                          Event2{0},
                          Event0{2},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event2{2},
                                 });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Delay by 2") {
    auto f = MakeDelayFixture(2);

    SECTION("Equal timestamps") {
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{
                          Event2{0},
                      });

        REQUIRE(f.FeedEvents({
                    Event0{1},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{1},
                }) == OutVec{
                          Event2{1},
                      });

        REQUIRE(f.FeedEvents({
                    Event0{2},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{2},
                }) == OutVec{
                          Event0{2},
                          Event2{2},
                      });

        REQUIRE(f.FeedEvents({
                    Event2{3},
                }) == OutVec{
                          Event0{3},
                          Event2{3},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event0{4},
                                 });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{1},
                }) == OutVec{
                          Event2{1},
                      });
        REQUIRE(f.FeedEvents({
                    Event0{2},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{3},
                }) == OutVec{
                          Event0{2},
                          Event2{3},
                      });

        REQUIRE(f.FeedEvents({
                    Event0{4},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event2{5},
                }) == OutVec{
                          Event0{4},
                          Event2{5},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event0{6},
                                 });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("Hasten by 2") {
    auto f = MakeHastenFixture(2);

    SECTION("Equal timestamps") {
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{0},
                }) == OutVec{
                          Event0{-2},
                      });

        REQUIRE(f.FeedEvents({
                    Event2{1},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{1},
                }) == OutVec{
                          Event0{-1},
                      });

        REQUIRE(f.FeedEvents({
                    Event2{2},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{2},
                }) == OutVec{
                          Event0{0},
                      });

        REQUIRE(f.FeedEvents({
                    Event0{3},
                }) == OutVec{
                          Event2{0},
                          Event0{1},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event2{1},
                                     Event2{2},
                                 });
        REQUIRE(f.DidEnd());
    }

    SECTION("Increment of 1") {
        REQUIRE(f.FeedEvents({
                    Event2{0},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{1},
                }) == OutVec{
                          Event0{-1},
                      });
        REQUIRE(f.FeedEvents({
                    Event2{2},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{3},
                }) == OutVec{
                          Event2{0},
                          Event0{1},
                      });

        REQUIRE(f.FeedEvents({
                    Event2{4},
                }) == OutVec{});
        REQUIRE(f.FeedEvents({
                    Event0{5},
                }) == OutVec{
                          Event2{2},
                          Event0{3},
                      });

        REQUIRE(f.FeedEnd({}) == OutVec{
                                     Event2{4},
                                 });
        REQUIRE(f.DidEnd());
    }
}

TEST_CASE("DelayHastenProcessor Sanity", "[DelayHastenProcessor]") {
    Macrotime delta = GENERATE(-2, -1, 0, 1, 2);
    auto f = MakeDelayHastenFixture(delta);

    OutVec o = f.FeedEvents({
        Event2{-3},
        Event0{0},
        Event2{3},
        Event0{6},
    });
    OutVec o1 = f.FeedEnd({});

    std::copy(o1.cbegin(), o1.cend(), std::back_inserter(o));

    REQUIRE(o == OutVec{
                     Event2{-3},
                     Event0{0 + delta},
                     Event2{3},
                     Event0{6 + delta},
                 });

    REQUIRE(f.DidEnd());
}
