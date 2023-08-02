/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "event_set.hpp"
#include "span.hpp"
#include "vector_queue.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace tcspc {

namespace internal {

template <typename EventSet> class capture_output {
    vector_queue<event_variant<EventSet>> output;
    bool ended = false;
    std::exception_ptr error;
    bool end_of_life = false; // Reported error; cannot reuse
    bool suppress_output;

    void dump_output(std::ostream &stream) {
        while (!output.empty()) {
            stream << "found output: ";
            std::visit([&](auto &&e) { stream << e << '\n'; }, output.front());
            output.pop();
        }
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
                    std::ostringstream stream;
                    stream << "captured output not checked\n";
                    dump_output(stream);
                    std::fputs(stream.str().c_str(), stderr);
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
            std::ostringstream stream;
            stream << "exception thrown while storing output: " << exc.what()
                   << '\n';
            std::fputs(stream.str().c_str(), stderr);
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
    auto retrieve() -> std::optional<Event> {
        assert(!end_of_life);
        if (not output.empty()) {
            auto const *event = std::get_if<Event>(&output.front());
            if (event) {
                auto const ret = Event(*event);
                output.pop();
                return ret;
            }
        }
        end_of_life = true;
        if (!suppress_output) {
            std::ostringstream stream;
            stream << "expected output of specific type\n";
            if (output.empty()) {
                stream << "found no output\n";
            }
            dump_output(stream);
            std::fputs(stream.str().c_str(), stderr);
        }
        return std::nullopt;
    }

    template <typename Event,
              typename = std::enable_if_t<contains_event_v<EventSet, Event>>>
    auto check(Event const &event) -> bool {
        assert(!end_of_life);
        event_variant<EventSet> expected = event;
        if (!output.empty() && output.front() == expected) {
            output.pop();
            return true;
        }
        end_of_life = true;
        if (!suppress_output) {
            std::ostringstream stream;
            stream << "expected output: " << event << '\n';
            if (output.empty()) {
                stream << "found no output\n";
            }
            dump_output(stream);
            std::fputs(stream.str().c_str(), stderr);
        }
        return false;
    }

    [[nodiscard]] auto check_not_end() -> bool {
        assert(!end_of_life);
        if (!output.empty()) {
            end_of_life = true;
            if (!suppress_output) {
                std::ostringstream stream;
                stream << "expected no output\n";
                dump_output(stream);
                std::fputs(stream.str().c_str(), stderr);
            }
            return false;
        }
        if (ended) {
            end_of_life = true;
            if (!suppress_output) {
                std::fputs("expected not end-of-stream\n", stderr);
                std::fputs("found end-of-stream\n", stderr);
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
                std::ostringstream stream;
                stream << "expected end-of-stream\n";
                dump_output(stream);
                std::fputs(stream.str().c_str(), stderr);
            }
            return false;
        }
        if (!ended) {
            end_of_life = true;
            if (!suppress_output) {
                std::fputs("expected end-of-stream\n", stderr);
                std::fputs("found no output\n", stderr);
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
                std::fputs("expected end-of-stream\n", stderr);
                std::fputs("found no output\n", stderr);
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
                std::fputs("expected not end-of-stream\n", stderr);
                std::fputs("found end-of-stream\n", stderr);
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
 * \brief Like std::vector, but with a stream insertion operator.
 *
 * \ingroup misc
 *
 * This can be used in place of \c std::vector as test events for batching and
 * buffering processors.
 *
 * \tparam T element type
 */
template <typename T> class pvector : public std::vector<T> {
  public:
    using std::vector<T>::vector;

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &stream, pvector vec)
        -> std::ostream & {
        stream << "pvector{ ";
        for (auto const &item : vec)
            stream << item << ", ";
        return stream << "}";
    }
};

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

/**
 * \brief Bit-cast an array of bytes to an event in little-endian order.
 *
 * \ingroup misc
 *
 * This is a helper for writing readable unit tests for raw device events that
 * are documented in little-endian byte order.
 *
 * The given array, which should be in big-endian order, is reversed and cast
 * to the type \c Event (which must be trivial).
 *
 * (There is currently no analogous \c be_event because device events specified
 * in big-endian order have not been encountered.)
 *
 * \tparam Event the returned event type
 *
 * \param bytes bytes constituting the event, in big-endian order
 */
template <typename Event>
inline auto le_event(std::array<std::uint8_t, sizeof(Event)> bytes) noexcept {
    static_assert(std::is_trivial_v<Event>);
    auto const srcspan = as_bytes(span(&bytes, 1));
    Event ret{};
    auto const retspan = as_writable_bytes(span(&ret, 1));
    std::reverse_copy(srcspan.begin(), srcspan.end(), retspan.begin());
    return ret;
}

} // namespace tcspc
