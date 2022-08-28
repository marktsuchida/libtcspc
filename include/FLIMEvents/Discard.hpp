#pragma once

#include "EventSet.hpp"

#include <exception>
#include <type_traits>

namespace flimevt {

class DiscardAny {
  public:
    template <typename E> void HandleEvent(E const &) noexcept {}

    void HandleEnd(std::exception_ptr) noexcept {}
};

template <typename ESet> class DiscardAll {
  public:
    template <typename E, typename = std::enable_if_t<ContainsEventV<ESet, E>>>
    void HandleEvent(E const &) noexcept {}

    void HandleEnd(std::exception_ptr) noexcept {}
};

} // namespace flimevt
