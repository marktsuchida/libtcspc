/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "introspect.hpp"
#include "processor_context.hpp"
#include "processor_traits.hpp"
#include "span.hpp"
#include "type_list.hpp"
#include "variant_event.hpp"
#include "vector_queue.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

namespace tcspc {

/**
 * \brief Accessor for \c capture_output processors.
 *
 * \ingroup processors-testing
 *
 * \see capture_output_checker
 */
class capture_output_accessor {
    std::any peek_events_func; // () -> std::vector<variant_event<EventList>>
    std::function<void()> pop_event_func;
    std::function<bool()> is_empty_func;
    std::function<bool()> is_flushed_func;
    std::function<void(std::size_t, bool)> set_up_to_throw_func;
    std::function<std::string()> events_as_string_func;

    template <typename EventList>
    auto peek_events() const -> std::vector<variant_event<EventList>> {
        return std::any_cast<
            std::function<std::vector<variant_event<EventList>>()>>(
            peek_events_func)();
    }

  public:
    /**
     * \brief Tag struct used internally for construction.
     */
    struct empty_event_list_tag {};

    /**
     * \brief Constructor used internally by \c capture_output.
     */
    template <typename EventList>
    explicit capture_output_accessor(
        std::function<std::vector<variant_event<EventList>>()> peek_events,
        std::function<void()> pop_event, std::function<bool()> is_empty,
        std::function<bool()> is_flushed,
        std::function<void(std::size_t, bool)> set_up_to_throw,
        std::function<std::string()> events_as_string)
        : peek_events_func(std::move(peek_events)),
          pop_event_func(std::move(pop_event)),
          is_empty_func(std::move(is_empty)),
          is_flushed_func(std::move(is_flushed)),
          set_up_to_throw_func(std::move(set_up_to_throw)),
          events_as_string_func(std::move(events_as_string)) {}

    /**
     * \brief Constructor used internally by \c capture_output.
     */
    explicit capture_output_accessor(
        [[maybe_unused]] empty_event_list_tag tag,
        std::function<bool()> is_flushed,
        std::function<void(std::size_t, bool)> set_up_to_throw)
        : is_empty_func([] { return true; }),
          is_flushed_func(std::move(is_flushed)),
          set_up_to_throw_func(std::move(set_up_to_throw)) {}

    /**
     * \brief Ensure that this accessor works with the given event set.
     *
     * \tparam EventList event set to check
     */
    template <typename EventList> void check_event_list() const {
        if constexpr (type_list_size_v<EventList> > 0) {
            (void)std::any_cast<
                std::function<std::vector<variant_event<EventList>>()>>(
                peek_events_func);
        }
    }

    /**
     * \brief Check if ready for input; used internally by \c feed_input.
     */
    void check_ready_for_input() const {
        if (not is_empty_func()) {
            throw std::logic_error(
                "cannot accept input: recorded output events remain unchecked:" +
                events_as_string_func());
        }
        if (is_flushed_func()) {
            throw std::logic_error(
                "cannot accept input: output has been flushed");
        }
    }

    /**
     * \brief Retrieve the next recorded output event.
     *
     * This can be used when \c check() is not convenient (for example, because
     * the exactly matching event is not known).
     *
     * \tparam Event the expected event type
     *
     * \tparam EventList the event set accepted by the \c capture_output
     * processor
     *
     * \return the event
     */
    template <
        typename Event, typename EventList,
        typename = std::enable_if_t<type_list_contains_v<EventList, Event>>>
    auto pop() -> Event {
        auto events = peek_events<EventList>();
        if (events.empty()) {
            throw std::logic_error(
                "tried to retrieve recorded output event of type " +
                std::string(typeid(Event).name()) + " but found no events");
        }
        auto const *event = std::get_if<Event>(&events.front());
        if (event == nullptr) {
            std::ostringstream stream;
            stream << "tried to retrieve recorded output event of type "
                   << std::string(typeid(Event).name()) << " but found:";
            for (auto const &event : events)
                stream << '\n' << event;
            throw std::logic_error(stream.str());
        }
        pop_event_func();
        return *event;
    }

    /**
     * \brief Check that the next recorded output event matches with the given
     * one.
     *
     * This function never returns false; a \c std::logic_error is thrown if
     * the check is unsuccessful. It returns true for convenient use with
     * testing framework macros such as \c CHECK() or \c REQUIRE() (which
     * typically help locate where an exception was thrown).
     *
     * \tparam Event the expected event type
     *
     * \tparam EventList the event set accepted by the \c capture_output
     * processor
     *
     * \param expected_event the expected event
     *
     * \return true if the check was successful
     */
    template <
        typename Event, typename EventList,
        typename = std::enable_if_t<type_list_contains_v<EventList, Event>>>
    auto check(Event const &expected_event) -> bool {
        auto events = peek_events<EventList>();
        if (events.empty()) {
            std::ostringstream stream;
            stream << "expected recorded output event " << expected_event
                   << " but found no events";
            throw std::logic_error(stream.str());
        }
        auto const *event = std::get_if<Event>(&events.front());
        if (event == nullptr || *event != expected_event) {
            std::ostringstream stream;
            stream << "expected recorded output event " << expected_event
                   << " but found:";
            for (auto const &event : events)
                stream << '\n' << event;
            throw std::logic_error(stream.str());
        }
        pop_event_func();
        return true;
    }

    /**
     * \brief Check that no recorded output events remain but the output has
     * not been flushed.
     *
     * This function never returns false; a \c std::logic_error is thrown if
     * the check is unsuccessful. It returns true for convenient use with
     * testing framework macros such as \c CHECK() or \c REQUIRE() (which
     * typically help locate where an exception was thrown).
     *
     * \return true if the check was successful.
     */
    auto check_not_flushed() -> bool {
        if (not is_empty_func()) {
            throw std::logic_error(
                "expected no recorded output events but found:" +
                events_as_string_func());
        }
        if (is_flushed_func()) {
            throw std::logic_error(
                "expected output unflushed but found flushed");
        }
        return true;
    }

    /**
     * \brief Check that no recorded output events remain and the output has
     * been flushed.
     *
     * This function never returns false; a \c std::logic_error is thrown if
     * the check is unsuccessful. It returns true for convenient use with
     * testing framework macros such as \c CHECK() or \c REQUIRE() (which
     * typically help locate where an exception was thrown).
     *
     * \return true if the check was successful.
     */
    auto check_flushed() -> bool {
        if (not is_empty_func()) {
            throw std::logic_error(
                "expected no recorded output events but found:" +
                events_as_string_func());
        }
        if (not is_flushed_func()) {
            throw std::logic_error(
                "expected output flushed but found unflushed");
        }
        return true;
    }

    /**
     * \brief Arrange to throw an error on receiving the given number of
     * events.
     *
     * \param count number of events to handle normally before throwing
     */
    void throw_error_on_next(std::size_t count = 0) {
        set_up_to_throw_func(count, true);
    }

    /**
     * \brief Arrange to throw an \c end_processing on receiving the given
     * number of events.
     *
     * \param count number of events to handle normally before throwing
     */
    void throw_end_processing_on_next(std::size_t count = 0) {
        set_up_to_throw_func(count, false);
    }

    /**
     * \brief Arrange to throw an error on receiving a flush.
     */
    void throw_error_on_flush() {
        set_up_to_throw_func(std::numeric_limits<std::size_t>::max(), true);
    }

    /**
     * \brief Arrange to throw an \c end_processing on receiving a flush.
     */
    void throw_end_processing_on_flush() {
        set_up_to_throw_func(std::numeric_limits<std::size_t>::max(), false);
    }
};

/**
 * \brief Event-set-specific wrapper for \c capture_output_accessor.
 *
 * \ingroup processors-testing
 *
 * This class has almost the same interface as \c capture_output_accessor but
 * is parameterized on \c EventList so does not require specifying the event
 * set when calling \c check() or \c pop().
 *
 * \see capture_output_accessor
 */
template <typename EventList> class capture_output_checker {
    capture_output_accessor acc;

  public:
    /**
     * \brief Construct from a \c capture_output_accessor.
     */
    explicit capture_output_checker(capture_output_accessor accessor)
        : acc(std::move(accessor)) {
        acc.check_event_list<EventList>(); // Fail early.
    }

    /**
     * \brief Retrieve the next recorded output event.
     *
     * This can be used when \c check() is not convenient (for example, because
     * the exactly matching event is not known).
     *
     * \tparam Event the expected event type
     *
     * \return the event
     */
    template <typename Event, typename = std::enable_if_t<
                                  type_list_contains_v<EventList, Event>>>
    auto pop() -> Event {
        return acc.pop<Event, EventList>();
    }

    /**
     * \brief Check that the next recorded output event matches with the given
     * one.
     *
     * This function never returns false; a \c std::logic_error is thrown if
     * the check is unsuccessful. It returns true for convenient use with
     * testing framework macros such as \c CHECK() or \c REQUIRE() (which
     * typically help locate where an exception was thrown).
     *
     * \tparam Event the expected event type
     *
     * \param expected_event the expected event
     *
     * \return true if the check was successful
     */
    template <typename Event, typename = std::enable_if_t<
                                  type_list_contains_v<EventList, Event>>>
    auto check(Event const &expected_event) -> bool {
        return acc.check<Event, EventList>(expected_event);
    }

    /**
     * \brief Check that no recorded output events remain but the output has
     * not been flushed.
     *
     * This function never returns false; a \c std::logic_error is thrown if
     * the check is unsuccessful. It returns true for convenient use with
     * testing framework macros such as \c CHECK() or \c REQUIRE() (which
     * typically help locate where an exception was thrown).
     *
     * \return true if the check was successful.
     */
    auto check_not_flushed() -> bool { return acc.check_not_flushed(); }

    /**
     * \brief Check that no recorded output events remain and the output has
     * been flushed.
     *
     * This function never returns false; a \c std::logic_error is thrown if
     * the check is unsuccessful. It returns true for convenient use with
     * testing framework macros such as \c CHECK() or \c REQUIRE() (which
     * typically help locate where an exception was thrown).
     *
     * \return true if the check was successful.
     */
    auto check_flushed() -> bool { return acc.check_flushed(); }

    /**
     * \brief Arrange to throw an error on receiving the given number of
     * events.
     *
     * \param count number of events to handle normally before throwing
     */
    void throw_error_on_next(std::size_t count = 0) {
        acc.throw_error_on_next(count);
    }

    /**
     * \brief Arrange to throw an \c end_processing on receiving the given
     * number of events.
     *
     * \param count number of events to handle normally before throwing
     */
    void throw_end_processing_on_next(std::size_t count = 0) {
        acc.throw_end_processing_on_next(count);
    }

    /**
     * \brief Arrange to throw an error on receiving a flush.
     */
    void throw_error_on_flush() { acc.throw_error_on_flush(); }

    /**
     * \brief Arrange to throw an \c end_processing on receiving a flush.
     */
    void throw_end_processing_on_flush() {
        acc.throw_end_processing_on_flush();
    }
};

namespace internal {

template <typename EventList> class capture_output {
    vector_queue<variant_event<EventList>> output;
    bool flushed = false;
    std::size_t error_in = std::numeric_limits<std::size_t>::max();
    std::size_t end_in = std::numeric_limits<std::size_t>::max();
    bool error_on_flush = false;
    bool end_on_flush = false;

    processor_tracker<capture_output_accessor> trk;

  public:
    explicit capture_output(
        processor_tracker<capture_output_accessor> &&tracker)
        : trk(std::move(tracker)) {
        trk.register_accessor_factory([](auto &tracker) {
            auto *self =
                LIBTCSPC_PROCESSOR_FROM_TRACKER(capture_output, trk, tracker);
            return capture_output_accessor(
                std::function([self] { return self->peek(); }),
                [self] { self->output.pop(); },
                [self] { return self->output.empty(); },
                [self] { return self->flushed; },
                [self](std::size_t count, bool use_error) {
                    self->set_up_to_throw(count, use_error);
                },
                [self] { return self->events_as_string(); });
        });
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "capture_output");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = processor_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename Event, typename = std::enable_if_t<
                                  type_list_contains_v<EventList, Event>>>
    void handle(Event const &event) {
        assert(not flushed);
        if (error_in == 0)
            throw std::runtime_error("test error upon event");
        output.push(event);
        if (end_in == 0)
            throw end_processing("test end-of-stream upon event");
        --error_in;
        --end_in;
    }

    void flush() {
        assert(not flushed);
        if (error_on_flush) {
            throw std::runtime_error("test error upon flush");
        }
        flushed = true;
        if (end_on_flush)
            throw end_processing("test end-of-stream upon flush");
    }

  private:
    auto peek() const -> std::vector<variant_event<EventList>> {
        std::vector<variant_event<EventList>> ret;
        output.for_each([&ret](auto const &event) { ret.push_back(event); });
        return ret;
    }

    [[nodiscard]] auto events_as_string() const -> std::string {
        std::ostringstream stream;
        output.for_each([&](auto const &event) { stream << '\n' << event; });
        return stream.str();
    }

    void set_up_to_throw(std::size_t count, bool use_error) {
        if (count == std::numeric_limits<std::size_t>::max()) {
            if (use_error)
                error_on_flush = true;
            else
                end_on_flush = true;
        } else {
            if (use_error)
                error_in = count;
            else
                end_in = count;
        }
    }
};

// Specialization for empty event list.
template <> class capture_output<type_list<>> {
    bool flushed = false;
    bool error_on_flush = false;
    bool end_on_flush = false;
    processor_tracker<capture_output_accessor> trk;

  public:
    explicit capture_output(
        processor_tracker<capture_output_accessor> &&tracker)
        : trk(std::move(tracker)) {
        trk.register_accessor_factory([](auto &tracker) {
            auto *self =
                LIBTCSPC_PROCESSOR_FROM_TRACKER(capture_output, trk, tracker);
            return capture_output_accessor(
                capture_output_accessor::empty_event_list_tag{},
                [self] { return self->flushed; },
                [self](std::size_t count, bool use_error) {
                    self->set_up_to_throw(count, use_error);
                });
        });
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "capture_output");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = processor_graph();
        g.push_entry_point(this);
        return g;
    }

    void flush() {
        assert(not flushed);
        if (error_on_flush) {
            throw std::runtime_error("test error upon flush");
        }
        flushed = true;
        if (end_on_flush)
            throw end_processing("test end-of-stream upon flush");
    }

  private:
    void set_up_to_throw(std::size_t count, bool use_error) {
        if (count == std::numeric_limits<std::size_t>::max()) {
            if (use_error)
                error_on_flush = true;
            else
                end_on_flush = true;
        }
    }
};

template <typename EventList, typename Downstream> class feed_input {
    std::vector<std::pair<std::shared_ptr<processor_context>, std::string>>
        outputs_to_check; // (context, name)
    Downstream downstream;

    static_assert(
        handles_events_v<Downstream, EventList>,
        "processor under test must handle the specified input events");
    static_assert(handles_flush_v<Downstream>,
                  "processor under test must handle flushing");

    void check_outputs_ready() {
        if (outputs_to_check.empty())
            throw std::logic_error(
                "feed_input has no registered capture_output to check");
        for (auto &[context, name] : outputs_to_check)
            context->template accessor<capture_output_accessor>(name)
                .check_ready_for_input();
    }

  public:
    explicit feed_input(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "feed_input");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_source(this);
        return g;
    }

    void require_output_checked(std::shared_ptr<processor_context> context,
                                std::string name) {
        context->accessor<capture_output_accessor>(name); // Fail early.
        outputs_to_check.emplace_back(context, std::move(name));
    }

    template <typename Event, typename = std::enable_if_t<
                                  type_list_contains_v<EventList, Event>>>
    void feed(Event const &event) {
        check_outputs_ready();
        downstream.handle(event);
    }

    void flush() {
        check_outputs_ready();
        downstream.flush();
    }
};

} // namespace internal

/**
 * \brief Create a sink that logs test output for checking.
 *
 * \ingroup processors-testing
 *
 * \tparam EventList event set to accept
 *
 * \param tracker processor tracker for later access to state
 *
 * \return capture-output sink
 */
template <typename EventList>
auto capture_output(processor_tracker<capture_output_accessor> &&tracker) {
    return internal::capture_output<EventList>(std::move(tracker));
}

/**
 * \brief Create a source for feeding test input to a processor.
 *
 * \ingroup processors-testing
 *
 * \tparam EventList input event set
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return feed-input source
 */
template <typename EventList, typename Downstream>
auto feed_input(Downstream &&downstream) {
    return internal::feed_input<EventList, Downstream>(
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
 * \brief Processors that sinks only specific events.
 *
 * \ingroup processors-testing
 *
 * This can be used to check at compile time that output of the upstream
 * processor does not contain unexpected events.
 *
 * \tparam EventList event types to allow
 */
template <typename EventList> class sink_events {
  public:
    /** \brief Processor interface */
    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "sink_events");
        return info;
    }

    /** \brief Processor interface */
    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = processor_graph();
        g.push_entry_point(this);
        return g;
    }

    /** \brief Processor interface */
    template <typename Event, typename = std::enable_if_t<
                                  type_list_contains_v<EventList, Event>>>
    void handle([[maybe_unused]] Event const &event) {}

    /** \brief Processor interface */
    void flush() {}
};

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
