#include "FLIMEvents/TimeDelay.hpp"

#include "FLIMEvents/Common.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <utility>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

using Events = Events01;
using OutVec = std::vector<EventVariant<Events>>;

auto MakeTimeDelayFixture(Macrotime delta) {
    return MakeProcessorTestFixture<Events, Events>(
        [delta](auto &&downstream) {
            return TimeDelay(delta, std::move(downstream));
        });
}

TEST_CASE("Time delay", "[TimeDelay]") {
    SECTION("Zero delay is noop") {
        auto f = MakeTimeDelayFixture(0);
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{0},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Delay +1") {
        auto f = MakeTimeDelayFixture(1);
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{1},
                              });
        f.FeedEvents({
            Event<1>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<1>{2},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }

    SECTION("Delay -1") {
        auto f = MakeTimeDelayFixture(-1);
        f.FeedEvents({
            Event<0>{0},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<0>{-1},
                              });
        f.FeedEvents({
            Event<1>{1},
        });
        REQUIRE(f.Output() == OutVec{
                                  Event<1>{0},
                              });
        f.FeedEnd({});
        REQUIRE(f.Output() == OutVec{});
        REQUIRE(f.DidEnd());
    }
}