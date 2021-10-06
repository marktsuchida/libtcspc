#pragma once

#include "EventSet.hpp"

#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

// Interface for processor consuming Events
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

// Internal impl of VirtualWrappedProcessor
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

// Wrap Proc in dynamically polymorphic class implementing
// VirtualProcessor<Events...>
template <typename Proc, typename... Events>
using VirtualWrappedProcessor =
    VirtualWrappedProcessorImpl<VirtualProcessor<Events...>, Proc, Events...>;

// Wrap dynamically polymorphic proc in static class
template <typename... Events> class PolymorphicProcessor {
    std::shared_ptr<VirtualProcessor<Events...>> proc;

  public:
    PolymorphicProcessor(std::shared_ptr<VirtualProcessor<Events...>> proc)
        : proc(proc) {}

    // Rule of zero

    template <typename E,
              typename = std::enable_if_t<(... || std::is_same_v<E, Events>)>>
    void HandleEvent(E const &event) noexcept {
        proc->HandleEvent(event);
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        proc->HandleEnd(error);

        // No more calls will be made to proc, so avoid holding onto it
        proc.reset();
    }
};

} // namespace internal

template <typename ESet>
using VirtualProcessor =
    typename ESet::template TemplateOfEvents<internal::VirtualProcessor>;

template <typename ESet>
using PolymorphicProcessor =
    typename ESet::template TemplateOfEvents<internal::PolymorphicProcessor>;

template <typename Proc, typename ESet>
using VirtualWrappedProcessor =
    typename ESet::template TemplateOfEvents<internal::VirtualWrappedProcessor,
                                             Proc>;

} // namespace flimevt
