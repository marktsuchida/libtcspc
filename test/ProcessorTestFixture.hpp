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

template <typename OutputEventSet> class logging_mock_processor {
  public:
    struct result {
        std::vector<event_variant<OutputEventSet>> outputs;
        bool did_end = false;
        std::exception_ptr error;
    };

  private:
    result &outputs;

  public:
    explicit logging_mock_processor(result &outputs) noexcept
        : outputs(outputs) {}

    template <typename E> void handle_event(E const &event) noexcept {
        assert(!outputs.did_end);
        try {
            outputs.outputs.push_back(event);
        } catch (std::exception const &exc) {
            UNSCOPED_INFO("Failed to store output: " << exc.what());
        }
    }

    void handle_end(std::exception_ptr error) noexcept {
        assert(!outputs.did_end);
        outputs.did_end = true;
        outputs.error = error;
    }
};

} // namespace internal

// Wrap a processor-under-test so that output events resulting from each
// (sequence of) input event can be examined.
template <typename InputEventSet, typename OutputEventSet, typename Proc>
class processor_test_fixture {
    using mock_downstream_type =
        internal::logging_mock_processor<OutputEventSet>;

    Proc proc;
    typename mock_downstream_type::result result;

    template <typename F>
    explicit processor_test_fixture(F proc_factory)
        : proc(proc_factory(mock_downstream_type(result))) {}

  public:
    // Create an instance.
    // Creation requires a factory function template in order to deduce Proc
    // while using the given InputEventSet and OutputEventSet.
    template <typename IES, typename OES, typename F>
    friend auto make_processor_test_fixture(F proc_factory);

    using output_vector_type = std::vector<event_variant<OutputEventSet>>;

    // Feed multiple events (old-style); all past outputs must have been
    // checked
    void feed_events(std::vector<event_variant<InputEventSet>> inputs) {
        if (!result.outputs.empty())
            throw std::logic_error("Unchecked output remains");
        for (auto const &input : inputs) {
            std::visit([&](auto &&i) { proc.handle_event(i); }, input);
        }
    }

    // Feed one event; all past outputs must have been checked
    template <typename E> void feed(E const &event) {
        static_assert(contains_event_v<InputEventSet, E>);
        if (!result.outputs.empty())
            throw std::logic_error("Unchecked output remains");
        proc.handle_event(event);
    }

    // Feed "end of stream" and return the resulting output events
    void feed_end(std::exception_ptr error) {
        if (!result.outputs.empty())
            throw std::logic_error("Unchecked output remains");
        proc.handle_end(error);
    }

    // Old-style output checking (requires operator<< on variant to print)
    output_vector_type output() {
        auto ret = result.outputs;
        result.outputs.clear();
        return ret;
    }

    // New-style output checking
    template <typename E> bool check(E const &event) {
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
    bool did_end() {
        if (!result.outputs.empty())
            throw std::logic_error("Unchecked output remains");
        if (result.error)
            std::rethrow_exception(result.error);
        return result.did_end;
    }
};

// Create a processor_test_fixture. proc_factory must be a callable taking an
// rvalue ref to a downstream processor and returning an instance of the
// processor-under-test.
template <typename InputEventSet, typename OutputEventSet, typename F>
auto make_processor_test_fixture(F proc_factory) {
    using mock_downstream_type =
        internal::logging_mock_processor<OutputEventSet>;
    using Proc = std::invoke_result_t<F, mock_downstream_type>;

    return processor_test_fixture<InputEventSet, OutputEventSet, Proc>(
        proc_factory);
}

} // namespace flimevt::test
