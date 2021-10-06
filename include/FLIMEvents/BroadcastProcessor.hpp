#pragma once

#include <exception>
#include <tuple>
#include <utility>

namespace flimevt {

template <typename... Ds> class BroadcastProcessor {
    std::tuple<Ds...> downstreams;

  public:
    explicit BroadcastProcessor(Ds &&...downstreams)
        : downstreams{std::move<Ds>(downstreams)...} {}

    template <typename E> void HandleEvent(E const &event) noexcept {
        std::apply([&](auto &...s) { (..., s.HandleEvent(event)); },
                   downstreams);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        std::apply([&](auto &...s) { (..., s.HandleEnd(error)); },
                   downstreams);
    }
};

} // namespace flimevt
