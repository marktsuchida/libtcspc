#include "FLIMEvents/Count.hpp"

#include "FLIMEvents/EventSet.hpp"
#include "ProcessorTestFixture.hpp"
#include "TestEvents.hpp"

#include <cstdint>
#include <type_traits>
#include <vector>

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

using Input = Event<0>;
using Output = Event<1>;
using Reset = Event<2>;
using Other = Event<3>;
using Events = Events0123;
using OutVec = std::vector<EventVariant<Events>>;

template <bool EmitAfter>
auto MakeCountEventFixture(std::uint64_t threshold, std::uint64_t limit) {
    return MakeProcessorTestFixture<Events, Events>(
        [threshold, limit](auto &&downstream) {
            using D = std::remove_reference_t<decltype(downstream)>;
            return CountEvent<Input, Reset, Output, EmitAfter, D>(
                threshold, limit, std::move(downstream));
        });
}

TEST_CASE("Count event", "[CountEvent]") {
    SECTION("Threshold 0, limit 1") {
        SECTION("Emit before") {
            auto f = MakeCountEventFixture<false>(0, 1);
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Output{42},
                                      Input{42},
                                  });
            f.FeedEvents({
                Input{43},
            });
            REQUIRE(f.Output() == OutVec{
                                      Output{43},
                                      Input{43},
                                  });
            f.FeedEvents({
                Reset{44},
            });
            REQUIRE(f.Output() == OutVec{
                                      Reset{44},
                                  });
            f.FeedEvents({
                Input{45},
            });
            REQUIRE(f.Output() == OutVec{
                                      Output{45},
                                      Input{45},
                                  });
            f.FeedEvents({
                Other{46},
            });
            REQUIRE(f.Output() == OutVec{
                                      Other{46},
                                  });
            f.FeedEnd({});
            REQUIRE(f.Output() == OutVec{});
            REQUIRE(f.DidEnd());
        }

        SECTION("Emit after") {
            auto f = MakeCountEventFixture<true>(0, 1);
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{42},
                                  });
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{42},
                                  });
            f.FeedEnd({});
            REQUIRE(f.Output() == OutVec{});
            REQUIRE(f.DidEnd());
        }
    }

    SECTION("Threshold 1, limit 1") {
        SECTION("Emit before") {
            auto f = MakeCountEventFixture<false>(1, 1);
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{42},
                                  });
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{42},
                                  });
            f.FeedEnd({});
            REQUIRE(f.Output() == OutVec{});
            REQUIRE(f.DidEnd());
        }

        SECTION("Emit after") {
            auto f = MakeCountEventFixture<true>(1, 1);
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{42},
                                      Output{42},
                                  });
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{42},
                                      Output{42},
                                  });
            f.FeedEnd({});
            REQUIRE(f.Output() == OutVec{});
            REQUIRE(f.DidEnd());
        }
    }

    SECTION("Threshold 1, limit 2") {
        SECTION("Emit before") {
            auto f = MakeCountEventFixture<false>(1, 2);
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{42},
                                  });
            f.FeedEvents({
                Input{43},
            });
            REQUIRE(f.Output() == OutVec{
                                      Output{43},
                                      Input{43},
                                  });
            f.FeedEvents({
                Input{44},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{44},
                                  });
            f.FeedEvents({
                Reset{},
            });
            REQUIRE(f.Output() == OutVec{
                                      Reset{},
                                  });
            f.FeedEvents({
                Input{45},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{45},
                                  });
            f.FeedEvents({
                Input{46},
            });
            REQUIRE(f.Output() == OutVec{
                                      Output{46},
                                      Input{46},
                                  });
            f.FeedEnd({});
            REQUIRE(f.Output() == OutVec{});
            REQUIRE(f.DidEnd());
        }

        SECTION("Emit after") {
            auto f = MakeCountEventFixture<true>(1, 2);
            f.FeedEvents({
                Input{42},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{42},
                                      Output{42},
                                  });
            f.FeedEvents({
                Input{43},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{43},
                                  });
            f.FeedEvents({
                Input{44},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{44},
                                      Output{44},
                                  });
            f.FeedEvents({
                Reset{},
            });
            REQUIRE(f.Output() == OutVec{
                                      Reset{},
                                  });
            f.FeedEvents({
                Input{45},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{45},
                                      Output{45},
                                  });
            f.FeedEvents({
                Input{46},
            });
            REQUIRE(f.Output() == OutVec{
                                      Input{46},
                                  });
            f.FeedEnd({});
            REQUIRE(f.Output() == OutVec{});
            REQUIRE(f.DidEnd());
        }
    }
}
