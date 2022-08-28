#include "FLIMEvents/RoutePhotons.hpp"

#include "FLIMEvents/Discard.hpp"
#include "ProcessorTestFixture.hpp"

#include <catch2/catch.hpp>

using namespace flimevt;
using namespace flimevt::test;

auto MakeRoutePhotonsFixtureOutput0(
    std::vector<std::int16_t> const &channels) {
    auto makeProc = [&channels](auto &&downstream) {
        auto discard = DiscardAll<TCSPCEvents>();
        return RoutePhotons(channels, std::move(downstream),
                            std::move(discard));
    };

    return MakeProcessorTestFixture<TCSPCEvents, TCSPCEvents>(makeProc);
}

auto MakeRoutePhotonsFixtureOutput1(
    std::vector<std::int16_t> const &channels) {
    auto makeProc = [&channels](auto &&downstream) {
        auto discard = DiscardAll<TCSPCEvents>();
        return RoutePhotons(channels, std::move(discard),
                            std::move(downstream));
    };

    return MakeProcessorTestFixture<TCSPCEvents, TCSPCEvents>(makeProc);
}

auto MakeRoutePhotonsFixtureOutput2(
    std::vector<std::int16_t> const &channels) {
    auto makeProc = [&channels](auto &&downstream) {
        auto discard0 = DiscardAll<TCSPCEvents>();
        auto discard1 = DiscardAll<TCSPCEvents>();
        return RoutePhotons(channels, std::move(discard0), std::move(discard1),
                            std::move(downstream));
    };

    return MakeProcessorTestFixture<TCSPCEvents, TCSPCEvents>(makeProc);
}

using OutVec = std::vector<EventVariant<TCSPCEvents>>;

TEST_CASE("Route photons", "[RoutePhotons]") {
    auto f0 = MakeRoutePhotonsFixtureOutput0({5, -3});
    f0.FeedEvents({
        ValidPhotonEvent{{{100}, 123, 5}},
    });
    REQUIRE(f0.Output() == OutVec{
                               ValidPhotonEvent{{{100}, 123, 5}},
                           });
    f0.FeedEvents({
        ValidPhotonEvent{{{101}, 123, -3}},
    });
    REQUIRE(f0.Output() == OutVec{});
    f0.FeedEvents({
        ValidPhotonEvent{{{102}, 124, 0}},
    });
    REQUIRE(f0.Output() == OutVec{});
    f0.FeedEvents({
        MarkerEvent{{103}, 0},
    });
    REQUIRE(f0.Output() == OutVec{
                               MarkerEvent{{103}, 0},
                           });
    f0.FeedEnd({});
    REQUIRE(f0.Output() == OutVec{});
    REQUIRE(f0.DidEnd());

    auto f1 = MakeRoutePhotonsFixtureOutput1({5, -3});
    f1.FeedEvents({
        ValidPhotonEvent{{{100}, 123, 5}},
    });
    REQUIRE(f1.Output() == OutVec{});
    f1.FeedEvents({
        ValidPhotonEvent{{{101}, 123, -3}},
    });
    REQUIRE(f1.Output() == OutVec{
                               ValidPhotonEvent{{{101}, 123, -3}},
                           });
    f1.FeedEvents({
        ValidPhotonEvent{{{102}, 124, 0}},
    });
    REQUIRE(f1.Output() == OutVec{});
    f1.FeedEvents({
        MarkerEvent{{103}, 0},
    });
    REQUIRE(f1.Output() == OutVec{
                               MarkerEvent{{103}, 0},
                           });
    f1.FeedEnd({});
    REQUIRE(f1.Output() == OutVec{});
    REQUIRE(f1.DidEnd());

    auto f2 = MakeRoutePhotonsFixtureOutput2({5, -3});
    f2.FeedEvents({
        ValidPhotonEvent{{{100}, 123, 5}},
    });
    REQUIRE(f2.Output() == OutVec{});
    f2.FeedEvents({
        ValidPhotonEvent{{{101}, 123, -3}},
    });
    REQUIRE(f2.Output() == OutVec{});
    f2.FeedEvents({
        ValidPhotonEvent{{{102}, 124, 0}},
    });
    REQUIRE(f2.Output() == OutVec{});
    f2.FeedEvents({
        MarkerEvent{{103}, 0},
    });
    REQUIRE(f2.Output() == OutVec{
                               MarkerEvent{{103}, 0},
                           });
    f2.FeedEnd({});
    REQUIRE(f2.Output() == OutVec{});
    REQUIRE(f2.DidEnd());
}
