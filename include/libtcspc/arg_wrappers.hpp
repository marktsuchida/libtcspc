/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>

namespace tcspc {

/**
 * \brief Argument wrappers
 * \ingroup arg-wrappers
 */
namespace arg {

// Function argument wrappers for strong typing (avoiding mistakes with
// otherwise easily swappable parameters). Each has a constructor for two
// reasons: (1) To avoid default-constructing without initializing the value
// and (2) so that CTAD works.
// Ordered alphabetically.

/**
 * \brief Function argument wrapper for abstime parameter.
 */
template <typename T> struct abstime {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit abstime(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for bin width parameter.
 */
template <typename T> struct bin_width {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit bin_width(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for count parameter.
 */
template <typename T> struct count {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit count(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for delay parameter.
 */
template <typename T> struct delay {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit delay(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for initial count parameter.
 */
template <typename T> struct initial_count {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit initial_count(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for interval parameter.
 */
template <typename T> struct interval {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit interval(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for limit parameter.
 */
template <typename T> struct limit {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit limit(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for maximum bin index parameter.
 */
template <typename T> struct max_bin_index {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_bin_index(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for maximum bin value.
 */
template <typename T> struct max_per_bin {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_per_bin(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for number of bins parameter.
 */
template <typename T = std::size_t> struct num_bins {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit num_bins(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for number of elements parameter.
 */
template <typename T = std::size_t> struct num_elements {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit num_elements(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for offset parameter.
 */
template <typename T> struct offset {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit offset(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for start parameter.
 */
template <typename T> struct start {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit start(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for stop parameter.
 */
template <typename T> struct stop {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit stop(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for threshold parameter.
 */
template <typename T> struct threshold {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit threshold(T arg) : value(arg) {}
};

} // namespace arg

} // namespace tcspc
