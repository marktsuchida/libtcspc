/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "data_types.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"

#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Event, typename OutEvent, typename Matcher,
          bool PassMatched, typename Downstream>
class match {
    static_assert(is_processor_v<Downstream, Event, OutEvent>);

    Matcher matcher;
    Downstream downstream;

  public:
    explicit match(Matcher matcher, Downstream downstream)
        : matcher(std::move(matcher)), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "match");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename E, typename = std::enable_if_t<
                              handles_event_v<Downstream, remove_cvref_t<E>>>>
    void handle(E &&event) {
        if constexpr (std::is_convertible_v<remove_cvref_t<E>, Event>) {
            auto const abstime = event.abstime;
            bool const matched = matcher(event);
            bool const pass = PassMatched ? true : not matched;
            if (pass)
                downstream.handle(std::forward<E>(event));
            if (matched)
                downstream.handle(OutEvent{abstime});
        } else {
            downstream.handle(std::forward<E>(event));
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Like `tcspc::match()`, but do not pass through matched events.
 *
 * \ingroup processors-timing
 *
 * All behavior is the same as `tcspc::match()`, except that input events that
 * are matched are discarded.
 *
 * \see `tcspc::match()`
 *
 * \par Events handled
 * - `Event`: pass through only if not a match; if a match, emit `OutEvent`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename Event, typename OutEvent, typename Matcher,
          typename Downstream>
auto match_replace(Matcher matcher, Downstream downstream) {
    return internal::match<Event, OutEvent, Matcher, false, Downstream>(
        std::move(matcher), std::move(downstream));
}

/**
 * \brief Create a processor that detects events matching a criterion.
 *
 * \ingroup processors-timing
 *
 * All events are passed through.
 *
 * Any event of type \p Event is tested by the given \p matcher (see \ref
 * matchers). If it is a match, an \p OutEvent is generated with the same
 * `abstime` as the \p Event.
 *
 * Both \p Event and \p OutEvent must have a `abstime` field.
 *
 * \see `tcspc::match_replace()`
 *
 * \tparam Event event type to match
 *
 * \tparam OutEvent event type to emit on match
 *
 * \tparam Matcher type of matcher (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param matcher the matcher
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: pass through; if a match, emit `OutEvent`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename Event, typename OutEvent, typename Matcher,
          typename Downstream>
auto match(Matcher matcher, Downstream downstream) {
    return internal::match<Event, OutEvent, Matcher, true, Downstream>(
        std::move(matcher), std::move(downstream));
}

/**
 * \brief Matcher that matches all events.
 *
 * \ingroup matchers
 */
class always_matcher {
  public:
    /** \brief Implements matcher requirement; return true. */
    template <typename Event>
    auto operator()(Event const & /* event */) const -> bool {
        return true;
    }
};

/**
 * \brief Matcher that matches no event.
 *
 * \ingroup matchers
 */
class never_matcher {
  public:
    /** \brief Implements matcher requirement; return false. */
    template <typename Event>
    auto operator()(Event const & /* event */) const -> bool {
        return false;
    }
};

/**
 * \brief Matcher that matches a single channel.
 *
 * \ingroup matchers
 *
 * The events to be matched must contain a `channel` field.
 *
 * \tparam DataTypes data type set specifying `channel_type`
 */
template <typename DataTypes = default_data_types> class channel_matcher {
    typename DataTypes::channel_type channel;

  public:
    /**
     * \brief Construct with the given \p channel to match.
     */
    explicit channel_matcher(
        arg::channel<typename DataTypes::channel_type> channel)
        : channel(channel.value) {}

    /** \brief Implements matcher requirement. */
    template <typename Event>
    auto operator()(Event const &event) const -> bool {
        static_assert(std::is_same_v<decltype(event.channel),
                                     typename DataTypes::channel_type>);
        return event.channel == channel;
    }
};

} // namespace tcspc
