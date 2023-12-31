/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Event, typename OutEvent, typename Matcher,
          bool PassMatched, typename Downstream>
class match {
    Matcher matcher;
    Downstream downstream;

  public:
    explicit match(Matcher matcher, Downstream downstream)
        : matcher(std::move(matcher)), downstream(std::move(downstream)) {}

    void handle(Event const &event) {
        bool matched = matcher(event);
        if constexpr (PassMatched) {
            downstream.handle(event);
        } else {
            if (not matched)
                downstream.handle(event);
        }
        if (matched)
            downstream.handle(OutEvent{event.abstime});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Like \ref match, but do not pass through matched events.
 *
 * \ingroup processors-timing
 *
 * All behavior is the same as \c match, except that input events that are
 * matched are discarded.
 *
 * \see match
 */
template <typename Event, typename OutEvent, typename Matcher,
          typename Downstream>
auto match_replace(Matcher &&matcher, Downstream &&downstream) {
    return internal::match<Event, OutEvent, Matcher, false, Downstream>(
        std::forward<Matcher>(matcher), std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that detects events matching a criterion.
 *
 * \ingroup processors-timing
 *
 * All events are passed through.
 *
 * Any event of type \c Event is tested by the given \e matcher. If it is a
 * matched, an \c OutEvent is generated with the same abstime.
 *
 * Both \c Event and \c OutEvent must have a \c abstime field.
 *
 * For matchers provided by libtcspc, see \ref matchers.
 *
 * \see match_replace
 *
 * \tparam Event event type to match
 *
 * \tparam OutEvent event type to emit on match
 *
 * \tparam Matcher type of matcher
 *
 * \tparam Downstream downstream processor type
 *
 * \param matcher the matcher
 *
 * \param downstream downstream processor
 *
 * \return match processor
 *
 * \inevents
 * \event{Event, passed through; triggers OutEvent if matched}
 * \event{All other events, passed through}
 * \endevents
 *
 * \outevents
 * \event{Event, passed through}
 * \event{OutEvent, emitted after a matched event}
 * \event{Other events, passed through}
 * \endevents
 */
template <typename Event, typename OutEvent, typename Matcher,
          typename Downstream>
auto match(Matcher &&matcher, Downstream &&downstream) {
    return internal::match<Event, OutEvent, Matcher, true, Downstream>(
        std::forward<Matcher>(matcher), std::forward<Downstream>(downstream));
}

/**
 * \brief Matcher that always matches.
 *
 * \ingroup matchers
 *
 * This allows \c match to be used used to unconditionally convert events of a
 * certain type.
 *
 * \see never_matcher
 */
class always_matcher {
  public:
    /** \brief Matcher interface. */
    template <typename Event>
    auto operator()([[maybe_unused]] Event const &event) const noexcept
        -> bool {
        return true;
    }
};

/**
 * \brief Matcher that never matches.
 *
 * \ingroup matchers
 *
 * Provided for symmetry with \c always_matcher.
 *
 * \see always_matcher
 */
class never_matcher {
  public:
    /** \brief Matcher interface. */
    template <typename Event>
    auto operator()([[maybe_unused]] Event const &event) const noexcept
        -> bool {
        return false;
    }
};

/**
 * \brief Matcher that matches a single channel.
 *
 * \ingroup matchers
 *
 * \tparam DataTraits traits type specifying \c channel_type
 */
template <typename DataTraits = default_data_traits> class channel_matcher {
    typename DataTraits::channel_type channel;

  public:
    /**
     * \brief Construct with a channel number.
     *
     * \param channel the channel number to match
     */
    explicit channel_matcher(typename DataTraits::channel_type channel)
        : channel(channel) {}

    /** \brief Matcher interface. */
    template <typename Event>
    auto operator()(Event const &event) const noexcept -> bool {
        static_assert(std::is_same_v<decltype(event.channel),
                                     typename DataTraits::channel_type>);
        return event.channel == channel;
    }
};

} // namespace tcspc
