/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "FLIMEvents/EventSet.hpp"

#include <catch2/catch.hpp>

#include <cassert>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace flimevt::test {

namespace internal {

template <typename OutputEventSet> class LoggingMockProcessor {
  public:
    struct Result {
        std::vector<event_variant<OutputEventSet>> outputs;
        bool didEnd = false;
        std::exception_ptr error;
    };

  private:
    Result &outputs;

  public:
    explicit LoggingMockProcessor(Result &outputs) noexcept
        : outputs(outputs) {}

    template <typename E> void handle_event(E const &event) noexcept {
        assert(!outputs.didEnd);
        try {
            outputs.outputs.push_back(event);
        } catch (std::exception const &exc) {
            UNSCOPED_INFO("Failed to store output: " << exc.what());
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
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

    using OutputVectorType = std::vector<event_variant<OutputEventSet>>;

    // Feed multiple events (old-style); all past outputs must have been
    // checked
    void FeedEvents(std::vector<event_variant<InputEventSet>> inputs) {
        if (!result.outputs.empty())
            throw std::logic_error("Unchecked output remains");
        for (auto const &input : inputs) {
            std::visit([&](auto &&i) { proc.handle_event(i); }, input);
        }
    }

    // Feed one event; all past outputs must have been checked
    template <typename E> void Feed(E const &event) {
        static_assert(contains_event_v<InputEventSet, E>);
        if (!result.outputs.empty())
            throw std::logic_error("Unchecked output remains");
        proc.handle_event(event);
    }

    // Feed "end of stream" and return the resulting output events
    void FeedEnd(std::exception_ptr error) {
        if (!result.outputs.empty())
            throw std::logic_error("Unchecked output remains");
        proc.handle_end(error);
    }

    // Old-style output checking (requires operator<< on variant to print)
    OutputVectorType Output() {
        auto ret = result.outputs;
        result.outputs.clear();
        return ret;
    }

    // New-style output checking
    template <typename E> bool Check(E const &event) {
        if (result.outputs.empty())
            throw std::logic_error("No output pending");
        event_variant<OutputEventSet> expected = event;
        event_variant<OutputEventSet> &output = result.outputs.front();
        bool ret = (output == expected);
        if (!ret) {
            std::cerr << "Expected output: " << event << '\n';
            std::visit(
                [&](auto &&o) { std::cerr << "Actual output: " << o << '\n'; },
                output);
        }
        result.outputs.erase(result.outputs.begin());
        return ret;
    }

    // Test whether the output reached "end of stream". Throws if end was
    // reached with an error.
    bool DidEnd() {
        if (!result.outputs.empty())
            throw std::logic_error("Unchecked output remains");
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
