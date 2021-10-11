#pragma once

#include "ApplyClassTemplateToTupleElements.hpp"
#include "EventSet.hpp"

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

template <typename... Events> class VirtualProcessor;

template <> class VirtualProcessor<> {
  public:
    virtual ~VirtualProcessor() = default;
    virtual void HandleEnd(std::exception_ptr) noexcept = 0;
};

template <typename Event0>
class VirtualProcessor<Event0> : public VirtualProcessor<> {
  public:
    virtual void HandleEvent(Event0 const &) noexcept = 0;
};

template <typename Event0, typename Event1, typename... Events>
class VirtualProcessor<Event0, Event1, Events...>
    : public VirtualProcessor<Event1, Events...> {
    using BaseType = VirtualProcessor<Event1, Events...>;

  public:
    using BaseType::HandleEvent; // Import overload set
    virtual void HandleEvent(Event0 const &) noexcept = 0;
};

template <typename Interface, typename Proc, typename... Events>
class VirtualWrappedProcessorImpl;

template <typename Interface, typename Proc>
class VirtualWrappedProcessorImpl<Interface, Proc> : public Interface {
  protected:
    Proc proc;

  public:
    explicit VirtualWrappedProcessorImpl(Proc &&proc)
        : proc(std::move(proc)) {}

    template <typename... Args>
    explicit VirtualWrappedProcessorImpl(Args &&...args)
        : proc(std::forward<Args>(args)...) {}

    void HandleEnd(std::exception_ptr error) noexcept final {
        proc.HandleEnd(error);
    }

    Proc &Wrapped() { return proc; }
};

template <typename Interface, typename Proc, typename Event0,
          typename... Events>
class VirtualWrappedProcessorImpl<Interface, Proc, Event0, Events...>
    : public VirtualWrappedProcessorImpl<Interface, Proc, Events...> {
    using BaseType = VirtualWrappedProcessorImpl<Interface, Proc, Events...>;

  protected:
    using BaseType::proc;

  public:
    using BaseType::HandleEvent; // Import overload set
    void HandleEvent(Event0 const &event) noexcept final {
        proc.HandleEvent(event);
    }
};

template <typename Proc, typename... Events>
using VirtualWrappedProcessor =
    VirtualWrappedProcessorImpl<VirtualProcessor<Events...>, Proc, Events...>;

} // namespace internal

// Interface for dynamically polymorphic processor consuming event set
template <typename ESet>
using VirtualProcessor =
    ApplyClassTemplateToTupleElementsT<internal::VirtualProcessor, ESet>;

// Wrap Proc in dynamically polymorphic class implementing
// VirtualProcessor<ESet>
template <typename Proc, typename ESet>
using VirtualWrappedProcessor =
    ApplyClassTemplateToTupleElementsT<internal::VirtualWrappedProcessor, ESet,
                                       Proc>;

// Wrap dynamically polymorphic proc in static class
template <typename ESet> class PolymorphicProcessor {
    std::shared_ptr<VirtualProcessor<ESet>> proc;

  public:
    PolymorphicProcessor(std::shared_ptr<VirtualProcessor<ESet>> proc)
        : proc(proc) {}

    // Rule of zero

    template <typename E, typename = std::enable_if_t<ContainsEventV<ESet, E>>>
    void HandleEvent(E const &event) noexcept {
        proc->HandleEvent(event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        proc->HandleEnd(error);

        // No more calls will be made to proc, so avoid holding onto it
        proc.reset();
    }
};

} // namespace flimevt
