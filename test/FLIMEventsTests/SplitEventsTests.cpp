#include "FLIMEvents/SplitEvents.hpp"

#include "FLIMEvents/NoopProcessor.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <type_traits>
#include <typeinfo>

#include <catch2/catch.hpp>

using namespace flimevt;

static_assert(HandlesEventSetV<SplitEvents<Events01, NoopProcessor<Events01>,
                                           NoopProcessor<Events01>>,
                               Events01>);

auto MakeSplitEventsFixtureOutput0() {
    auto makeProc = [](auto &&downstream) {
        using D0 = std::remove_reference_t<decltype(downstream)>;
        using D1 = NoopProcessor<Events23>;
        return SplitEvents<Events23, D0, D1>(std::move(downstream), D1());
    };

    return test::MakeProcessorTestFixture<Events0123, Events01>(makeProc);
}

auto MakeSplitEventsFixtureOutput1() {
    auto makeProc = [](auto &&downstream) {
        using D0 = NoopProcessor<Events01>;
        using D1 = std::remove_reference_t<decltype(downstream)>;
        return SplitEvents<Events23, D0, D1>(D0(), std::move(downstream));
    };

    return test::MakeProcessorTestFixture<Events0123, Events23>(makeProc);
}

using OutVec01 = std::vector<EventVariant<Events01>>;
using OutVec23 = std::vector<EventVariant<Events23>>;

TEST_CASE("Split events", "[SplitEvents]") {
    auto f0 = MakeSplitEventsFixtureOutput0();
    auto f1 = MakeSplitEventsFixtureOutput1();

    SECTION("Empty stream yields empty streams") {
        REQUIRE(f0.FeedEnd({}) == OutVec01{});
        REQUIRE(f0.DidEnd());
        REQUIRE(f1.FeedEnd({}) == OutVec23{});
        REQUIRE(f1.DidEnd());
    }

    SECTION("Errors propagate to both streams") {
        REQUIRE(f0.FeedEnd(std::make_exception_ptr(
                    std::runtime_error("test"))) == OutVec01{});
        REQUIRE_THROWS_MATCHES(f0.DidEnd(), std::runtime_error,
                               Catch::Message("test"));
        REQUIRE(f1.FeedEnd(std::make_exception_ptr(
                    std::runtime_error("test"))) == OutVec23{});
        REQUIRE_THROWS_MATCHES(f1.DidEnd(), std::runtime_error,
                               Catch::Message("test"));
    }

    SECTION("Events are split") {
        // Event<0> goes to output 0 only
        REQUIRE(f0.FeedEvents({
                    Event<0>{0},
                }) == OutVec01{
                          Event<0>{0},
                      });
        REQUIRE(f1.FeedEvents({
                    Event<0>{0},
                }) == OutVec23{});

        // Event<0> goes to output 1 only
        REQUIRE(f0.FeedEvents({
                    Event<2>{0},
                }) == OutVec01{});
        REQUIRE(f1.FeedEvents({
                    Event<2>{0},
                }) == OutVec23{
                          Event<2>{0},

                      });
    }
}
