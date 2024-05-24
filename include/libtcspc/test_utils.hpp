/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bucket.hpp"
#include "common.hpp"
#include "context.hpp"
#include "data_types.hpp"
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
#include <initializer_list>
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

namespace internal {

template <typename EventList> class sink_events {
  public:
    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "sink_events");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return processor_graph().push_entry_point(this);
    }

    template <typename E,
              typename = std::enable_if_t<is_convertible_to_type_list_member_v<
                  internal::remove_cvref_t<E>, EventList>>>
    void handle(E &&event) {
        [[maybe_unused]] std::remove_reference_t<E> const e =
            std::forward<E>(event);
    }

    void flush() {}
};

} // namespace internal

/**
 * \brief Create a processor that ignores only specific event types.
 *
 * \ingroup processors-testing
 *
 * This can be used for compile-time checks of the output event types of a
 * processor.
 *
 * \tparam Events event types to accept
 */
template <typename... Event> auto sink_events() {
    return internal::sink_events<type_list<Event...>>();
}

/**
 * \brief Create a processor that ignores only specific event types.
 *
 * \ingroup processors-testing
 *
 * This can be used for compile-time checks of the output event types of a
 * processor.
 *
 * \tparam EventList event types to accept
 */
template <typename EventList> auto sink_event_list() {
    return internal::sink_events<EventList>();
}

/**
 * \brief Value category used to feed an event via `tcspc::feed_input`.
 *
 * \ingroup misc
 */
enum class feed_as {
    /** \brief Feed as const lvalue. */
    const_lvalue,
    /** \brief Feed as non-const rvalue. */
    rvalue,
};

/** \private */
inline auto operator<<(std::ostream &stream, feed_as cat) -> std::ostream & {
    switch (cat) {
    case feed_as::const_lvalue:
        return stream << "feed_as::const_lvalue";
    case feed_as::rvalue:
        return stream << "feed_as::rvalue";
    }
}

/**
 * \brief Value category to check emitted events against.
 *
 * \ingroup misc
 *
 * \see `tcspc::capture_output_checker`
 */
enum class emitted_as {
    /** \brief Require const lvalue or rvalue, or non-const rvalue. */
    any_allowed,
    /** \brief Require the same category as the events being fed. */
    same_as_fed,
    /** \brief Require const lvalue. */
    always_lvalue,
    /** \brief Require non-const rvalue. */
    always_rvalue,
};

/** \private */
inline auto operator<<(std::ostream &stream,
                       emitted_as cat) -> std::ostream & {
    switch (cat) {
    case emitted_as::any_allowed:
        return stream << "emitted_as::any_allowed";
    case emitted_as::same_as_fed:
        return stream << "emitted_as::same_as_fed";
    case emitted_as::always_lvalue:
        return stream << "emitted_as::always_lvalue";
    case emitted_as::always_rvalue:
        return stream << "emitted_as::always_rvalue";
    }
}

namespace internal {

// Value category observed by capture_output.
enum class emitted_value_category {
    const_lvalue,
    nonconst_lvalue,
    const_rvalue,
    nonconst_rvalue,
};

inline void check_value_category(feed_as feed_cat, emitted_as expected,
                                 emitted_value_category actual) {
    if (actual == emitted_value_category::nonconst_lvalue)
        throw std::logic_error("non-const lvalue event not allowed");

    if (expected == emitted_as::same_as_fed) {
        switch (feed_cat) {
        case feed_as::const_lvalue:
            expected = emitted_as::always_lvalue;
            break;
        case feed_as::rvalue:
            expected = emitted_as::always_rvalue;
            break;
        }
    }

    switch (expected) {
    case emitted_as::any_allowed:
        break;
    case emitted_as::same_as_fed:
        unreachable();
    case emitted_as::always_lvalue:
        if (actual == emitted_value_category::nonconst_rvalue)
            throw std::logic_error("expected lvalue event, found rvalue");
        break;
    case emitted_as::always_rvalue:
        if (actual != emitted_value_category::nonconst_rvalue)
            throw std::logic_error("expected rvalue event, found lvalue");
        break;
    }
}

inline auto operator<<(std::ostream &stream,
                       emitted_value_category cat) -> std::ostream & {
    switch (cat) {
        using e = emitted_value_category;
    case e::const_lvalue:
        return stream << "const &";
    case e::nonconst_lvalue:
        return stream << "&";
    case e::const_rvalue:
        return stream << "const &&";
    case e::nonconst_rvalue:
        return stream << "&&";
    }
}

template <typename EventList>
using recorded_event =
    std::pair<emitted_value_category, variant_event<EventList>>;

template <typename EventList>
auto operator<<(std::ostream &stream,
                recorded_event<EventList> const &pair) -> std::ostream & {
    return stream << pair.second << ' ' << pair.first;
}

} // namespace internal

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
    std::any peek_events_func; // () -> std::vector<recorded_event<EventList>>
    std::function<void()> pop_event_func;
    std::function<bool()> is_empty_func;
    std::function<bool()> is_flushed_func;
    std::function<void(std::size_t, bool)> set_up_to_throw_func;
    std::function<std::string()> events_as_string_func;

    template <typename EventList>
    auto
    peek_events() const -> std::vector<internal::recorded_event<EventList>> {
        return std::any_cast<
            std::function<std::vector<internal::recorded_event<EventList>>()>>(
            peek_events_func)();
    }

  public:
    /** \private */
    struct empty_event_list_tag {};

    /** \private */
    template <typename EventList>
    explicit capture_output_access(
        std::function<std::vector<internal::recorded_event<EventList>>()>
            peek_events,
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
            (void)std::any_cast<std::function<
                std::vector<internal::recorded_event<EventList>>()>>(
                peek_events_func);
        }
    }

    /**
     * \brief Check if ready for input; normally used internally by
     * `tcspc::feed_input()`.
     */
    void check_ready_for_input(std::string const &input) const {
        if (not is_empty_func()) {
            throw std::logic_error(
                "cannot accept input (" + input +
                "): recorded output events remain unchecked:" +
                events_as_string_func());
        }
        if (is_flushed_func()) {
            throw std::logic_error("cannot accept input (" + input +
                                   "): output has been flushed");
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
     * \param feeder_value_category value category of events fed to the
     * processor under test
     *
     * \param value_category expected value category of emitted event being
     * popped
     *
     * \return the event
     */
    template <typename Event, typename EventList>
    auto pop(feed_as feeder_value_category,
             emitted_as value_category) -> Event {
        static_assert(type_list_contains_v<EventList, Event>);
        auto const events = peek_events<EventList>();
        try {
            if (events.empty())
                throw std::logic_error("missing event");
            check_value_category(feeder_value_category, value_category,
                                 events.front().first);
            auto const *event = std::get_if<Event>(&events.front().second);
            if (event == nullptr)
                throw std::logic_error("type mismatch");
            pop_event_func();
            return *event;
        } catch (std::logic_error const &exc) {
            std::ostringstream stream;
            stream << "event pop failed: " << exc.what() << '\n';
            stream << "expected recorded output event of type "
                   << std::string(typeid(Event).name()) << " ("
                   << feeder_value_category << ", " << value_category
                   << ") but found";
            if (events.empty()) {
                stream << " no events";
            } else {
                stream << ':';
                for (auto const &e : events)
                    stream << '\n' << e;
            }
            throw std::logic_error(stream.str());
        }
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
     * \param feeder_value_category value category of events fed to the
     * processor under test
     *
     * \param value_category expected value category of emitted event being
     * checked
     *
     * \param expected_event the expected event
     *
     * \return true if the check was successful
     */
    template <typename Event, typename EventList>
    auto check(feed_as feeder_value_category, emitted_as value_category,
               Event const &expected_event) -> bool {
        static_assert(type_list_contains_v<EventList, Event>);
        auto events = peek_events<EventList>();
        try {
            if (events.empty())
                throw std::logic_error("missing event");
            check_value_category(feeder_value_category, value_category,
                                 events.front().first);
            auto const *event = std::get_if<Event>(&events.front().second);
            if (event == nullptr)
                throw std::logic_error("type mismatch");
            if (*event != expected_event)
                throw std::logic_error("value mismatch");
            pop_event_func();
            return true;
        } catch (std::logic_error const &exc) {
            std::ostringstream stream;
            stream << "event check failed: " << exc.what() << '\n';
            stream << "expected recorded output event " << expected_event
                   << " (" << feeder_value_category << ", " << value_category
                   << ") but found";
            if (events.empty()) {
                stream << " no events";
            } else {
                stream << ':';
                for (auto const &e : events)
                    stream << '\n' << e;
            }
            throw std::logic_error(stream.str());
        }
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

    feed_as feeder_valcat;

  public:
    /**
     * \brief Construct from a `tcspc::capture_output_access`, with the
     * feeder's value category.
     */
    explicit capture_output_checker(feed_as feeder_value_category,
                                    capture_output_access access)
        : acc(std::move(access)), feeder_valcat(feeder_value_category) {
        acc.check_event_list<EventList>(); // Fail early.
    }

    /**
     * \brief Construct from a context, tracker name of
     * `tcspc::capture_output` processor, and feeder's value category.
     */
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    explicit capture_output_checker(feed_as feeder_value_category,
                                    std::shared_ptr<context> context,
                                    std::string const &name)
        : capture_output_checker(
              feeder_value_category,
              context->access<capture_output_access>(name)) {}

    /**
     * \brief Retrieve the next recorded output event, disregarding value
     * category.
     *
     * Equivalent to `pop(tcspc::emitted_as::any_allowed)`.
     */
    template <typename Event> auto pop() -> Event {
        return pop<Event>(emitted_as::any_allowed);
    }

    /**
     * \brief Retrieve the next recorded output event, checking its value
     * category.
     *
     * This can be used when `check()` is not convenient (for example, because
     * the exactly matching event is not known).
     *
     * \tparam Event the expected event type
     *
     * \return the event
     */
    template <typename Event> auto pop(emitted_as value_category) -> Event {
        static_assert(type_list_contains_v<EventList, Event>);
        return acc.pop<Event, EventList>(feeder_valcat, value_category);
    }

    /**
     * \brief Check that the next recorded output event matches with the given
     * event, disregarding value category.
     *
     * Equivalent to `check(tcspc::emitted_as::any_allowed, expected_event)`.
     */
    template <typename Event> auto check(Event const &expected_event) -> bool {
        return check(emitted_as::any_allowed, expected_event);
    }

    /**
     * \brief Check that the next recorded output event matches with the given
     * event and value category.
     *
     * This function never returns false; a `std::logic_error` is thrown if the
     * check is unsuccessful. It returns true for convenient use with testing
     * framework macros such as `CHECK()` or `REQUIRE()` (which typically help
     * locate where an exception was thrown).
     *
     * \tparam Event the expected event type
     *
     * \param value_category the expected value category of the emitted event;
     * `tcspc::emitted_as::same_as_fed` is only allowed if the feeder value
     * category was set upon construction
     *
     * \param expected_event the expected event
     *
     * \return true if the check was successful
     */
    template <typename Event>
    auto check(emitted_as value_category,
               Event const &expected_event) -> bool {
        static_assert(type_list_contains_v<EventList, Event>);
        return acc.check<Event, EventList>(feeder_valcat, value_category,
                                           expected_event);
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
    vector_queue<recorded_event<EventList>> output;
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
                                  EventList, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        static constexpr auto valcat = [] {
            if constexpr (std::is_lvalue_reference_v<Event>) {
                if constexpr (std::is_const_v<std::remove_reference_t<Event>>)
                    return emitted_value_category::const_lvalue;
                else
                    return emitted_value_category::nonconst_lvalue;
            } else {
                if constexpr (std::is_const_v<Event>)
                    return emitted_value_category::const_rvalue;
                else
                    return emitted_value_category::nonconst_rvalue;
            }
        }();
        assert(not flushed);
        if (error_in == 0)
            throw test_error("test error upon event");
        output.push(std::pair{valcat, std::forward<Event>(event)});
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
    auto peek() const {
        std::vector<recorded_event<EventList>> ret;
        ret.reserve(output.size());
        // Cannot use std::copy because vector_queue lacks iterators.
        output.for_each([&ret](auto const &pair) { ret.push_back(pair); });
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

template <typename Downstream> class feed_input {
    static_assert(is_processor_v<Downstream>);

    std::vector<std::pair<std::shared_ptr<context>, std::string>>
        outputs_to_check; // (context, name)
    feed_as refmode = feed_as::const_lvalue;
    Downstream downstream;

    void check_outputs_ready(std::string const &input) {
        if (outputs_to_check.empty())
            throw std::logic_error(
                "feed_input has no registered capture_output to check");
        for (auto &[context, name] : outputs_to_check)
            context->template access<capture_output_access>(name)
                .check_ready_for_input(input);
    }

  public:
    explicit feed_input(Downstream downstream)
        : downstream(std::move(downstream)) {}

    explicit feed_input(feed_as mode, Downstream downstream)
        : refmode(mode), downstream(std::move(downstream)) {}

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

    template <typename Event, typename = std::enable_if_t<handles_event_v<
                                  Downstream, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        check_outputs_ready("event of type " +
                            std::string(typeid(event).name()));

        if (refmode == feed_as::const_lvalue) {
            downstream.handle(static_cast<Event const &>(event));
        } else if constexpr (std::is_lvalue_reference_v<Event>) {
            remove_cvref_t<Event> copy(event);
            downstream.handle(std::move(copy));
        } else {
            downstream.handle(std::forward<Event>(event));
        }
    }

    void flush() {
        check_outputs_ready("flush");
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
 *   requested; record the event and its value category; throw
 *   `tcspc::end_of_processing` if stop simulation requested; otherwise record
 *   for later analysis
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
 * In addition to `handle()`, `flush()`, and introspection, the processor has
 * this member function:
 *
 * - `void require_output_checked(std::shared_ptr<tcspc::context>
 *   context, std::string name)`: register a `tcspc::capture_output` processor
 *   whose recorded output should be fully checked or popped before events (or
 *   flush) are fed.
 *
 * At least one output must be registered with this function before feeding
 * input events (via `handle()`), or else `std::logic_error` is thrown.
 *
 * Events are fed according to \p value_category, making a copy if necessary
 * (when the event is an lvalue and `tcspc::feed_as::rvalue` is requested).
 * Thus, the value category of the event passed to `handle()` does not affect
 * how it is fed to the processor under test.
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param value_category value category (kind of reference) used to feed event
 *
 * \param downstream downstream processor (processor under test)
 *
 * \return processor
 *
 * \par Events handled
 * - Any type handled by `Downstream`: check that registered outputs have no
 *   unchecked recorded events pending; pass through as `value_category`
 * - Flush: check that the registered outputs have been checked; pass through
 */
template <typename Downstream>
auto feed_input(feed_as value_category, Downstream &&downstream) {
    return internal::feed_input<Downstream>(
        value_category, std::forward<Downstream>(downstream));
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
    friend auto operator==(
        [[maybe_unused]] empty_test_event<N> const &lhs,
        [[maybe_unused]] empty_test_event<N> const &rhs) noexcept -> bool {
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
    friend auto
    operator==(time_tagged_test_event const &lhs,
               time_tagged_test_event const &rhs) noexcept -> bool {
        return lhs.abstime == rhs.abstime;
    }

    /** \brief Inequality comparison operator. */
    friend auto
    operator!=(time_tagged_test_event const &lhs,
               time_tagged_test_event const &rhs) noexcept -> bool {
        return not(lhs == rhs);
    }

    /** \brief Stream insertion operator. */
    friend auto operator<<(std::ostream &strm,
                           time_tagged_test_event const &e) -> std::ostream & {
        return strm << "time_tagged_test_event<" << N << ">{" << e.abstime
                    << "}";
    }
};

/**
 * \brief Create an ad-hoc `tcspc::bucket<T>` for testing, from a list of
 * values.
 *
 * \ingroup misc
 *
 * The returned bucket does not support storage extraction.
 */
template <typename T>
auto test_bucket(std::initializer_list<T> il) -> bucket<T> {
    struct test_storage {
        std::vector<T> v;
    };
    auto storage = test_storage{{il}};
    return bucket<T>(span(storage.v), std::move(storage));
}

/**
 * \brief Create an ad-hoc `tcspc::bucket<T>` for testing, from a span.
 *
 * \ingroup misc
 *
 * The returned bucket does not support storage extraction.
 */
template <typename T> auto test_bucket(span<T> s) -> bucket<T> {
    struct test_storage {
        std::vector<T> v;
    };
    auto storage = test_storage{std::vector<T>(s.begin(), s.end())};
    return bucket<T>(span(storage.v), std::move(storage));
}

/**
 * \brief Bucket source wrapper for unit testing.
 *
 * \ingroup bucket-sources
 *
 * This bucket source delegates bucket creation to a backing source. It fills
 * each new bucket with the specified value before returning.
 *
 * In addition, the number of buckets created can be queried.
 *
 * \tparam T the bucket data element type
 */
template <typename T> class test_bucket_source : public bucket_source<T> {
    std::shared_ptr<bucket_source<T>> src;
    T value;
    std::size_t count = 0;

    explicit test_bucket_source(
        std::shared_ptr<bucket_source<T>> backing_source, T fill_value)
        : src(std::move(backing_source)), value(std::move(fill_value)) {}

  public:
    /** \brief Create an instance. */
    static auto
    create(std::shared_ptr<bucket_source<T>> backing_source,
           T fill_value) -> std::shared_ptr<test_bucket_source<T>> {
        return std::shared_ptr<test_bucket_source<T>>(new test_bucket_source(
            std::move(backing_source), std::move(fill_value)));
    }

    /** \brief Implements bucket source requirement. */
    auto bucket_of_size(std::size_t size) -> bucket<T> override {
        auto b = src->bucket_of_size(size);
        std::fill(b.begin(), b.end(), value);
        ++count;
        return b;
    }

    /** \brief Return the number of buckets created so far. */
    [[nodiscard]] auto bucket_count() const noexcept -> std::size_t {
        return count;
    }

    /** \brief Implements sharable bucket source requirement. */
    [[nodiscard]] auto
    supports_shared_views() const noexcept -> bool override {
        return src->supports_shared_views();
    }

    /** \brief Implements sharable bucket source requirement. */
    [[nodiscard]] auto
    shared_view_of(bucket<T> const &bkt) -> bucket<T const> override {
        return src->shared_view_of(bkt);
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
