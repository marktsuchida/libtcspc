/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "context.hpp"
#include "errors.hpp"
#include "introspect.hpp"
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
 * \brief Access for `tcspc::capture_output()` processors.
 *
 * \note It is recommended to wrap this object in
 * `tcspc::capture_output_checker`, which provides a similar interface but
 * simplifies calling `check()` and `pop()`.
 *
 * \ingroup context-access
 */
class capture_output_access {
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
    /** \private */
    struct empty_event_list_tag {};

    /** \private */
    template <typename EventList>
    explicit capture_output_access(
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

    /** \private */
    explicit capture_output_access(
        [[maybe_unused]] empty_event_list_tag tag,
        std::function<bool()> is_flushed,
        std::function<void(std::size_t, bool)> set_up_to_throw)
        : is_empty_func([] { return true; }),
          is_flushed_func(std::move(is_flushed)),
          set_up_to_throw_func(std::move(set_up_to_throw)) {}

    /**
     * \brief Ensure that this access works with the given event set.
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
     * \brief Check if ready for input; normally used internally by
     * `tcspc::feed_input()`.
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
     * This can be used when `check()` is not convenient (for example, because
     * the exactly matching event is not known).
     *
     * \tparam Event the expected event type
     *
     * \tparam EventList the event set accepted by the
     * `tcspc::capture_output()` processor
     *
     * \return the event
     */
    template <typename Event, typename EventList> auto pop() -> Event {
        static_assert(type_list_contains_v<EventList, Event>);
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
     * This function never returns false; a `std::logic_error` is thrown if the
     * check is unsuccessful. It returns true for convenient use with testing
     * framework macros such as `CHECK()` or `REQUIRE()` (which typically help
     * locate where an exception was thrown).
     *
     * \tparam Event the expected event type
     *
     * \tparam EventList the event set accepted by the
     * `tcspc::capture_output()` processor
     *
     * \param expected_event the expected event
     *
     * \return true if the check was successful
     */
    template <typename Event, typename EventList>
    auto check(Event const &expected_event) -> bool {
        static_assert(type_list_contains_v<EventList, Event>);
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
     * This function never returns false; a `std::logic_error` is thrown if the
     * check is unsuccessful. It returns true for convenient use with testing
     * framework macros such as `CHECK()` or `REQUIRE()` (which typically help
     * locate where an exception was thrown).
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
     * This function never returns false; a `std::logic_error` is thrown if the
     * check is unsuccessful. It returns true for convenient use with testing
     * framework macros such as `CHECK()` or `REQUIRE()` (which typically help
     * locate where an exception was thrown).
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
     * \brief Arrange to throw `tcspc::test_error` on receiving the given
     * number of events.
     *
     * \param count number of events to handle normally before throwing
     */
    void throw_error_on_next(std::size_t count = 0) {
        set_up_to_throw_func(count, true);
    }

    /**
     * \brief Arrange to throw `tcspc::end_of_processing` on receiving the
     * given number of events.
     *
     * \param count number of events to handle normally before throwing
     */
    void throw_end_processing_on_next(std::size_t count = 0) {
        set_up_to_throw_func(count, false);
    }

    /**
     * \brief Arrange to throw `tcspc::test_error` on receiving a flush.
     */
    void throw_error_on_flush() {
        set_up_to_throw_func(std::numeric_limits<std::size_t>::max(), true);
    }

    /**
     * \brief Arrange to throw `tcspc::end_of_processing` on receiving a flush.
     */
    void throw_end_processing_on_flush() {
        set_up_to_throw_func(std::numeric_limits<std::size_t>::max(), false);
    }
};

/**
 * \brief Event-set-specific wrapper for `tcspc::capture_output_access`.
 *
 * \ingroup context-access
 *
 * This class has almost the same interface as `tcspc::capture_output_access`
 * but is parameterized on \p EventList so does not require specifying the
 * event set when calling `check()` or `pop()`.
 */
template <typename EventList> class capture_output_checker {
    capture_output_access acc;

  public:
    /**
     * \brief Construct from a `tcspc::capture_output_access`.
     */
    explicit capture_output_checker(capture_output_access access)
        : acc(std::move(access)) {
        acc.check_event_list<EventList>(); // Fail early.
    }

    /**
     * \brief Retrieve the next recorded output event.
     *
     * This can be used when `check()` is not convenient (for example, because
     * the exactly matching event is not known).
     *
     * \tparam Event the expected event type
     *
     * \return the event
     */
    template <typename Event> auto pop() -> Event {
        static_assert(type_list_contains_v<EventList, Event>);
        return acc.pop<Event, EventList>();
    }

    /**
     * \brief Check that the next recorded output event matches with the given
     * one.
     *
     * This function never returns false; a `std::logic_error` is thrown if the
     * check is unsuccessful. It returns true for convenient use with testing
     * framework macros such as `CHECK()` or `REQUIRE()` (which typically help
     * locate where an exception was thrown).
     *
     * \tparam Event the expected event type
     *
     * \param expected_event the expected event
     *
     * \return true if the check was successful
     */
    template <typename Event> auto check(Event const &expected_event) -> bool {
        static_assert(type_list_contains_v<EventList, Event>);
        return acc.check<Event, EventList>(expected_event);
    }

    /**
     * \brief Check that no recorded output events remain but the output has
     * not been flushed.
     *
     * This function never returns false; a `std::logic_error` is thrown if the
     * check is unsuccessful. It returns true for convenient use with testing
     * framework macros such as `CHECK()` or `REQUIRE()` (which typically help
     * locate where an exception was thrown).
     *
     * \return true if the check was successful.
     */
    auto check_not_flushed() -> bool { return acc.check_not_flushed(); }

    /**
     * \brief Check that no recorded output events remain and the output has
     * been flushed.
     *
     * This function never returns false; a `std::logic_error` is thrown if the
     * check is unsuccessful. It returns true for convenient use with testing
     * framework macros such as `CHECK()` or `REQUIRE()` (which typically help
     * locate where an exception was thrown).
     *
     * \return true if the check was successful.
     */
    auto check_flushed() -> bool { return acc.check_flushed(); }

    /**
     * \brief Arrange to throw `tcspc::test_error` on receiving the given
     * number of events.
     *
     * \param count number of events to handle normally before throwing
     */
    void throw_error_on_next(std::size_t count = 0) {
        acc.throw_error_on_next(count);
    }

    /**
     * \brief Arrange to throw `tcspc::end_of_processing` on receiving the
     * given number of events.
     *
     * \param count number of events to handle normally before throwing
     */
    void throw_end_processing_on_next(std::size_t count = 0) {
        acc.throw_end_processing_on_next(count);
    }

    /**
     * \brief Arrange to throw `tcspc::test_error` on receiving a flush.
     */
    void throw_error_on_flush() { acc.throw_error_on_flush(); }

    /**
     * \brief Arrange to throw `tcspc::end_of_processing` on receiving a flush.
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

    access_tracker<capture_output_access> trk;

  public:
    explicit capture_output(access_tracker<capture_output_access> &&tracker)
        : trk(std::move(tracker)) {
        trk.register_access_factory([](auto &tracker) {
            auto *self =
                LIBTCSPC_OBJECT_FROM_TRACKER(capture_output, trk, tracker);
            return capture_output_access(
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
        return processor_info(this, "capture_output");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return processor_graph().push_entry_point(this);
    }

    template <typename Event, typename = std::enable_if_t<type_list_contains_v<
                                  EventList, internal::remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        assert(not flushed);
        if (error_in == 0)
            throw test_error("test error upon event");
        output.push(std::forward<Event>(event));
        if (end_in == 0)
            throw end_of_processing("test end-of-stream upon event");
        --error_in;
        --end_in;
    }

    void flush() {
        assert(not flushed);
        if (error_on_flush) {
            throw test_error("test error upon flush");
        }
        flushed = true;
        if (end_on_flush)
            throw end_of_processing("test end-of-stream upon flush");
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
    access_tracker<capture_output_access> trk;

  public:
    explicit capture_output(access_tracker<capture_output_access> &&tracker)
        : trk(std::move(tracker)) {
        trk.register_access_factory([](auto &tracker) {
            auto *self =
                LIBTCSPC_OBJECT_FROM_TRACKER(capture_output, trk, tracker);
            return capture_output_access(
                capture_output_access::empty_event_list_tag{},
                [self] { return self->flushed; },
                [self](std::size_t count, bool use_error) {
                    self->set_up_to_throw(count, use_error);
                });
        });
    }

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "capture_output");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return processor_graph().push_entry_point(this);
    }

    void flush() {
        assert(not flushed);
        if (error_on_flush) {
            throw test_error("test error upon flush");
        }
        flushed = true;
        if (end_on_flush)
            throw end_of_processing("test end-of-stream upon flush");
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
    std::vector<std::pair<std::shared_ptr<context>, std::string>>
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
            context->template access<capture_output_access>(name)
                .check_ready_for_input();
    }

  public:
    explicit feed_input(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "feed_input");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void require_output_checked(std::shared_ptr<context> context,
                                std::string name) {
        context->access<capture_output_access>(name); // Fail early.
        outputs_to_check.emplace_back(std::move(context), std::move(name));
    }

    template <typename Event> void feed(Event const &event) {
        static_assert(type_list_contains_v<EventList, Event>);
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
 * \brief Create a sink that records the output of a processor under test.
 *
 * \ingroup processors-testing
 *
 * In order to access the recorded output or arrange to simulate errors and
 * end-of-processing, use a `tcspc::capture_output_access` (usually accessed
 * through the wrapper `tcspc::capture_output_checker`) retrieved from the
 * `tcspc::context` from which \p tracker was obtained..
 *
 * \tparam EventList event set to accept
 *
 * \param tracker access tracker for later access to state
 *
 * \return processor
 *
 * \par Events handled
 * - Types in `EventList`: throw `tcspc::test_error` if error simulation
 *   requested; record the event; throw `tcspc::end_of_processing` if stop
 *   simulation requested; otherwise record for later analysis
 * - Flush: throw `tcspc::test_error` if error simulation requested; record
 *   the flush; throw `tcspc::end_of_processing` if stop simulation requested;
 *   otherwise record for later analysis
 */
template <typename EventList>
auto capture_output(access_tracker<capture_output_access> &&tracker) {
    return internal::capture_output<EventList>(std::move(tracker));
}

/**
 * \brief Create a source for feeding test input to a processor under test.
 *
 * \ingroup processors-testing
 *
 * In addition to `flush()` and introspection, the processor has these member
 * functions:
 * - `void require_output_checked(std::shared_ptr<tcspc::context>
 *   context, std::string name)`: register a `tcspc::capture_output` processor
 *   whose recorded output should be fully checked before events (and flush)
 *   are fed
 * - `template <typename Event> void feed(Event const &event)`: feed an event
 *   into \p downstream after checking that registered outputs have been
 *   checked
 *
 * \tparam EventList input event set
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Flush: check that the registered outputs have been checked; pass through
 */
template <typename EventList, typename Downstream>
auto feed_input(Downstream &&downstream) {
    return internal::feed_input<EventList, Downstream>(
        std::forward<Downstream>(downstream));
}

/**
 * \brief Processors that sinks only specific events.
 *
 * \ingroup processors-testing
 *
 * This can be used to check at compile time that output of the upstream
 * processor does not contain unexpected events.
 *
 * \tparam EventList event types to accept (as either rvalue or const lvalue
 * reference, unless the event is also in \p RvalueOnlyEventList)
 *
 * \tparam RvalueOnlyEventList event types to accept only as rvalue reference
 */
template <typename EventList, typename RvalueOnlyEventList = type_list<>>
class sink_events {
  public:
    /** \brief Implements processor requirement. */
    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "sink_events");
    }

    /** \brief Implements processor requirement. */
    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return processor_graph().push_entry_point(this);
    }

    /** \brief Implements processor requirement. */
    template <
        typename E,
        typename = std::enable_if_t<
            (type_list_contains_v<RvalueOnlyEventList,
                                  internal::remove_cvref_t<E>> &&
             not std::is_lvalue_reference_v<E> && not std::is_const_v<E>) ||
            (not type_list_contains_v<RvalueOnlyEventList,
                                      internal::remove_cvref_t<E>> &&
             type_list_contains_v<EventList, internal::remove_cvref_t<E>>)>>
    void handle(E &&event) {
        [[maybe_unused]] std::remove_reference_t<E> const e =
            std::forward<E>(event);
    }

    /** \brief Implements processor requirement. */
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
 * \tparam DataTypes data type set specifying `abstime_type`
 */
template <int N, typename DataTypes = default_data_types>
struct time_tagged_test_event {
    /** \brief Timestamp. */
    typename DataTypes::abstime_type abstime;

    /** \brief Equality comparison operator. */
    friend auto operator==(time_tagged_test_event const &lhs,
                           time_tagged_test_event const &rhs) noexcept
        -> bool {
        return lhs.abstime == rhs.abstime;
    }

    /** \brief Inequality comparison operator. */
    friend auto operator!=(time_tagged_test_event const &lhs,
                           time_tagged_test_event const &rhs) noexcept
        -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm, time_tagged_test_event const &e)
        -> std::ostream & {
        return strm << "time_tagged_test_event<" << N << ">{" << e.abstime
                    << "}";
    }
};

/**
 * \brief Bit-cast an array of bytes to an event after reversing the order.
 *
 * \ingroup misc
 *
 * This is a helper for writing more readable unit tests for raw device events
 * that are specified in little-endian byte order.
 *
 * The given array of \p bytes, which should be in big-endian order, is
 * reversed and cast to the type \p Event (which must be trivial).
 *
 * (There is no analogous `be_event` because device events specified in
 * big-endian order have not been encountered.)
 *
 * \note It is not enforced that `sizeof(Event)` be a power of 2 (for which
 * endianness makes sense).
 *
 * \tparam Event the returned event type
 *
 * \param bytes bytes constituting the event, in big-endian order
 */
template <typename Event>
inline auto
from_reversed_bytes(std::array<std::uint8_t, sizeof(Event)> bytes) noexcept {
    static_assert(std::is_trivial_v<Event>);
    auto const srcspan = as_bytes(span(&bytes, 1));
    Event ret{};
    auto const retspan = as_writable_bytes(span(&ret, 1));
    std::reverse_copy(srcspan.begin(), srcspan.end(), retspan.begin());
    return ret;
}

} // namespace tcspc
