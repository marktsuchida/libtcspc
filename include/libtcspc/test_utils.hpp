/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "vector_queue.hpp"

#include <cassert>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename EventSet> class capture_output {
    vector_queue<event_variant<EventSet>> output;
    bool ended = false;
    std::exception_ptr error;
    bool end_of_life = false; // Reported error; cannot reuse
    bool suppress_output;

    void dump_output() {
        while (!output.empty()) {
            std::cerr << "found output: ";
            std::visit([&](auto &&e) { std::cerr << e << '\n'; },
                       output.front());
            output.pop();
        }
    }

    template <typename Event, typename Equal,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    auto check_impl(Event const &event, Equal is_equal) -> bool {
        assert(!end_of_life);
        event_variant<EventSet> expected = event;
        if (!output.empty() && is_equal(output.front(), expected)) {
            output.pop();
            return true;
        }
        end_of_life = true;
        if (!suppress_output) {
            std::cerr << "expected output: " << event << '\n';
            if (output.empty()) {
                std::cerr << "found no output\n";
            }
            dump_output();
        }
        return false;
    }

  public:
    explicit capture_output(bool suppress_output = false)
        : suppress_output(suppress_output) {}

    // Disallow move and copy because output check thunks will keep reference
    // to this.
    capture_output(capture_output const &) = delete;
    auto operator=(capture_output const &) = delete;
    capture_output(capture_output &&) = delete;
    auto operator=(capture_output &&) = delete;
    ~capture_output() = default;

    auto output_check_thunk() -> std::function<bool()> {
        return [this] {
            assert(!end_of_life);
            if (!output.empty()) {
                if (!suppress_output) {
                    std::cerr << "captured output not checked\n";
                    dump_output();
                }
                end_of_life = true;
                return false;
            }
            return true;
        };
    }

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    void handle_event(Event const &event) noexcept {
        assert(!end_of_life);
        assert(!ended);
        try {
            output.push(event);
        } catch (std::exception const &exc) {
            std::cerr << "exception thrown while storing output: "
                      << exc.what() << '\n';
            std::terminate();
        }
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        assert(!end_of_life);
        assert(!ended);
        ended = true;
        this->error = error;
    }

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    auto check(Event const &event) -> bool {
        return check_impl(event, std::equal_to<>());
    }

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    auto check_approx(Event const &event) -> bool {
        return check_impl(event, [](auto const &lhs, auto const &rhs) {
            return approximately_equal(lhs, rhs);
        });
    }

    [[nodiscard]] auto check_not_end() -> bool {
        assert(!end_of_life);
        if (!output.empty()) {
            end_of_life = true;
            if (!suppress_output) {
                std::cerr << "expected no output\n";
                dump_output();
            }
            return false;
        }
        if (ended) {
            end_of_life = true;
            if (!suppress_output) {
                std::cerr << "expected not end-of-stream\n";
                std::cerr << "found end-of-stream\n";
            }
            return false;
        }
        return true;
    }

    [[nodiscard]] auto check_end() -> bool {
        assert(!end_of_life);
        if (!output.empty()) {
            end_of_life = true;
            if (!suppress_output) {
                std::cerr << "expected end-of-stream\n";
                dump_output();
            }
            return false;
        }
        if (!ended) {
            end_of_life = true;
            if (!suppress_output) {
                std::cerr << "expected end-of-stream\n";
                std::cerr << "found no output\n";
            }
            return false;
        }

        if (error)
            std::rethrow_exception(error);
        return true;
    }
};

// Specialization required for empty event set
template <> class capture_output<event_set<>> {
    bool ended = false;
    std::exception_ptr error;
    bool end_of_life = false; // Reported error; cannot reuse
    bool suppress_output;

  public:
    explicit capture_output(bool suppress_output = false)
        : suppress_output(suppress_output) {}

    // Disallow move and copy because output check thunks will keep reference
    // to this.
    capture_output(capture_output const &src) = delete;
    capture_output(capture_output &&src) = delete;
    auto operator=(capture_output const &rhs) -> capture_output & = delete;
    auto operator=(capture_output &&rhs) -> capture_output & = delete;
    ~capture_output() = default;

    auto output_check_thunk() -> std::function<bool()> {
        return [this] {
            assert(!end_of_life);
            return true;
        };
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        assert(!end_of_life);
        assert(!ended);
        ended = true;
        this->error = error;
    }

    [[nodiscard]] auto check_end() -> bool {
        assert(!end_of_life);
        if (!ended) {
            end_of_life = true;
            if (!suppress_output) {
                std::cerr << "expected end-of-stream\n";
                std::cerr << "found no output\n";
            }
            return false;
        }

        if (error)
            std::rethrow_exception(error);
        return true;
    }

    [[nodiscard]] auto check_not_end() -> bool {
        assert(!end_of_life);
        if (ended) {
            end_of_life = true;
            if (!suppress_output) {
                std::cerr << "expected not end-of-stream\n";
                std::cerr << "found end-of-stream\n";
            }
            return false;
        }
        return true;
    }
};

template <typename EventSet, typename Downstream> class feed_input {
    std::vector<std::function<bool()>> output_checks;
    Downstream downstream;

    static_assert(
        handles_event_set_v<Downstream, EventSet>,
        "processor under test must handle the specified input events and end-of-stream");

    void require_outputs_checked() {
        if (output_checks.empty())
            throw std::logic_error(
                "feed_input has no registered capture_output to check");
        for (auto &check : output_checks) {
            if (!check())
                throw std::logic_error("unchecked output remains");
        }
    }

  public:
    explicit feed_input(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    template <typename CaptureOutputProc>
    void require_output_checked(CaptureOutputProc &output) {
        output_checks.push_back(output.output_check_thunk());
    }

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    void feed(Event const &event) {
        require_outputs_checked();
        downstream.handle_event(event);
    }

    void feed_end(std::exception_ptr const &error) {
        require_outputs_checked();
        downstream.handle_end(error);
    }

    void feed_end() { feed_end({}); }
};

} // namespace internal

/**
 * \brief Create a sink that logs test output for checking.
 *
 * \ingroup processors-testing
 *
 * \tparam EventSet event set to accept
 *
 * \return capture-output sink
 */
template <typename EventSet> auto capture_output() {
    return internal::capture_output<EventSet>();
}

/**
 * \brief Create a source for feeding test input to a processor.
 *
 * \ingroup processors-testing
 *
 * \tparam EventSet input event set
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return feed-input source
 */
template <typename EventSet, typename Downstream>
auto feed_input(Downstream &&downstream) {
    return internal::feed_input<EventSet, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Processors that sinks only events in the given set, and
 * end-of-stream.
 *
 * \ingroup processors-testing
 *
 * This can be used to check at compile time that output of the upstream
 * processor does not contain unexpected events.
 *
 * \tparam EventSet event types to allow
 */
template <typename EventSet> class event_set_sink {
  public:
    /** \brief Processor interface */
    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    void handle_event([[maybe_unused]] Event const &event) noexcept {}

    /** \brief Processor interface */
    void
    handle_end([[maybe_unused]] std::exception_ptr const &error) noexcept {}
};

namespace internal {

template <typename EventSet, typename Downstream> class check_event_set {
    Downstream downstream;

  public:
    explicit check_event_set(Downstream &&downstream)
        : downstream(std::move(downstream)) {}

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    void handle_event(Event const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that forwards all events in a given set only.
 *
 * \ingroup processors-testing
 *
 * This processor simply forwards all events downstream, but it is a compile
 * error if an input event is not in \c EventSet.
 *
 * \see require_event_set
 *
 * \tparam EventSet the set of events to forward
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor
 */
template <typename EventSet, typename Downstream>
auto check_event_set(Downstream &&downstream) {
    return internal::check_event_set<EventSet, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Empty event for testing.
 *
 * \ingroup events-testing
 *
 * \tparam N a number to distinguish event types
 */
template <int N> struct empty_test_event {
    /** \brief Equality comparison operator. */
    friend auto
    operator==([[maybe_unused]] empty_test_event<N> const &lhs,
               [[maybe_unused]] empty_test_event<N> const &rhs) noexcept
        -> bool {
        return true;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(empty_test_event<N> const &lhs,
                           empty_test_event<N> const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm,
                           [[maybe_unused]] empty_test_event<N> const &e)
        -> std::ostream & {
        return strm << "empty_test_event<" << N << ">";
    }
};

/**
 * \brief Timestamped event for testing.
 *
 * \ingroup events-testing
 *
 * \tparam N a number to distinguish event types.
 *
 * \tparam DataTraits traits type specifying \c abstime_type
 */
template <int N, typename DataTraits = default_data_traits>
struct timestamped_test_event {
    /** \brief Timestamp. */
    typename DataTraits::abstime_type abstime;

    /** \brief Equality comparison operator. */
    friend auto operator==(timestamped_test_event const &lhs,
                           timestamped_test_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(timestamped_test_event const &lhs,
                           timestamped_test_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, timestamped_test_event const &e)
        -> std::ostream & {
        return strm << "timestamped_test_event<" << N << ">{" << e.abstime
                    << "}";
    }
};

} // namespace tcspc
