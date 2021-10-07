#pragma once

#include "FLIMEvents/EventSet.hpp"

#include <catch2/catch.hpp>

#include <cassert>
#include <exception>
#include <sstream>
#include <utility>
#include <variant>
#include <vector>

namespace flimevt::test {

namespace internal {

template <typename OutputEventSet> class LoggingMockProcessor {
  public:
    struct Result {
        std::vector<EventVariant<OutputEventSet>> outputs;
        bool didEnd = false;
        std::exception_ptr error;
    };

  private:
    Result &outputs;

  public:
    explicit LoggingMockProcessor(Result &outputs) noexcept
        : outputs(outputs) {}

    template <typename E> void HandleEvent(E const &event) noexcept {
        assert(!outputs.didEnd);
        try {
            outputs.outputs.push_back(event);
        } catch (std::exception const &exc) {
            UNSCOPED_INFO("Failed to store output: " << exc.what());
        }
    }

    void HandleEnd(std::exception_ptr error) noexcept {
        assert(!outputs.didEnd);
        outputs.didEnd = true;
        outputs.error = error;
    }
};

} // namespace internal

// Wrap a processor-under-test so that output events resulting from each
// (sequence of) input event can be examined.
template <typename InputEventSet, typename OutputEventSet, typename Proc>
class ProcessorTestFixture {
    using MockDownstream = internal::LoggingMockProcessor<OutputEventSet>;

    Proc proc;
    typename MockDownstream::Result result;

    template <typename F>
    explicit ProcessorTestFixture(F procFactory)
        : proc(procFactory(MockDownstream(result))) {}

  public:
    // Create an instance.
    // Creation requires a factory function template in order to deduce Proc
    // while using the given InputEventSet and OutputEventSet.
    template <typename IES, typename OES, typename F>
    friend auto MakeProcessorTestFixture(F procFactory);

    using OutputVectorType = std::vector<EventVariant<OutputEventSet>>;

    // Feed the given input events and return the resulting output events
    OutputVectorType
    FeedEvents(std::vector<EventVariant<InputEventSet>> inputs) {
        result.outputs.clear();
        for (auto const &input : inputs) {
            std::visit([&](auto &&i) { proc.HandleEvent(i); }, input);
        }
        return result.outputs;
    }

    // Feed "end of stream" and return the resulting output events
    OutputVectorType FeedEnd(std::exception_ptr error) {
        result.outputs.clear();
        proc.HandleEnd(error);
        return result.outputs;
    }

    // Test whether the output reached "end of stream". Throws if end was
    // reached with an error.
    bool DidEnd() {
        if (result.error)
            std::rethrow_exception(result.error);
        return result.didEnd;
    }
};

// Create a ProcessorTestFixture. procFactory must be a callable taking an
// rvalue ref to a downstream processor and returning an instance of the
// processor-under-test.
template <typename InputEventSet, typename OutputEventSet, typename F>
auto MakeProcessorTestFixture(F procFactory) {
    using MockDownstream = internal::LoggingMockProcessor<OutputEventSet>;
    using Proc = std::invoke_result_t<F, MockDownstream>;

    return ProcessorTestFixture<InputEventSet, OutputEventSet, Proc>(
        procFactory);
}

} // namespace flimevt::test
