/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace tcspc {

// Function argument wrappers for strong typing (avoiding mistakes with
// otherwise easily swappable parameters). Each has a constructor for two
// reasons: (1) To avoid default-constructing without initializing the value
// and (2) so that CTAD works.
// Ordered alphabetically.

/**
 * \brief Function argument wrapper for abstime parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_abstime {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_abstime(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for bin width parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_bin_width {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_bin_width(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for count parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_count {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_count(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for delay parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_delay {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_delay(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for initial count parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_initial_count {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_initial_count(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for interval parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_interval {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_interval(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for limit parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_limit {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_limit(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for max bin index parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_max_bin_index {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_max_bin_index(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for offset parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_offset {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_offset(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for start parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_start {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_start(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for stop parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_stop {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_stop(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for threshold parameter.
 * \ingroup arg-wrappers
 */
template <typename T> struct arg_threshold {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit arg_threshold(T arg) : value(arg) {}
};

} // namespace tcspc
