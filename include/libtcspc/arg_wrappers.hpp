/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>

/**
 * \namespace tcspc::arg
 * \brief Argument wrappers
 * \ingroup arg-wrappers
 */
namespace tcspc::arg {

// Function argument wrappers for strong typing (avoiding mistakes with
// otherwise easily swappable parameters). Each has a constructor for two
// reasons: (1) To avoid default-constructing without initializing the value
// and (2) so that CTAD works.
// Ordered alphabetically.

/**
 * \brief Function argument wrapper for append parameter.
 */
template <typename T> struct append {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit append(T arg) : value(arg) {}
};

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
 * \brief Function argument wrapper for batch size parameter.
 */
template <typename T = std::size_t> struct batch_size {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit batch_size(T arg) : value(arg) {}
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
 * \brief Function argument wrapper for bucket size.
 */
template <typename T = std::size_t> struct bucket_size {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit bucket_size(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for channel parameter.
 */
template <typename T> struct channel {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit channel(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for clamp parameter.
 */
template <typename T> struct clamp {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit clamp(T arg) : value(arg) {}
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
 * \brief Function argument wrapper for count threshold parameter.
 */
template <typename T> struct count_threshold {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit count_threshold(T arg) : value(arg) {}
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
 * \brief Function argument wrapper for delta parameter.
 */
template <typename T> struct delta {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit delta(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for fraction parameter.
 */
template <typename T> struct fraction {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit fraction(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for granularity parameter.
 */
template <typename T = std::size_t> struct granularity {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit granularity(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for initially open parameter.
 */
template <typename T> struct initially_open {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit initially_open(T arg) : value(arg) {}
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
 * \brief Function argument wrapper for interval threshold parameter.
 */
template <typename T> struct interval_threshold {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit interval_threshold(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for length parameter.
 */
template <typename T> struct length {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit length(T arg) : value(arg) {}
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
 * \brief Function argument wrapper for maximum bucket count.
 */
template <typename T = std::size_t> struct max_bucket_count {
    /** \brief The argument vlue. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_bucket_count(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for maximum buffered parameter.
 */
template <typename T = std::size_t> struct max_buffered {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_buffered(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for maximum count parameter.
 */
template <typename T> struct max_count {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_count(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for maximum interval parameter.
 */
template <typename T> struct max_interval {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_interval(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for maximum length parameter.
 */
template <typename T> struct max_length {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_length(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for maximum MSE parameter.
 */
template <typename T> struct max_mse {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_mse(T arg) : value(arg) {}
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
 * \brief Function argument wrapper for maximum time shift parameter.
 */
template <typename T> struct max_time_shift {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit max_time_shift(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for minimum interval parameter.
 */
template <typename T> struct min_interval {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit min_interval(T arg) : value(arg) {}
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
 * \brief Function argument wrapper for start channel parameter.
 */
template <typename T> struct start_channel {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit start_channel(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for start offset parameter.
 */
template <typename T> struct start_offset {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit start_offset(T arg) : value(arg) {}
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

/**
 * \brief Function argument wrapper for tick index parameter.
 */
template <typename T> struct tick_index {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit tick_index(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for time window parameter.
 */
template <typename T> struct time_window {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit time_window(T arg) : value(arg) {}
};

/**
 * \brief Function argument wrapper for truncate parameter.
 */
template <typename T> struct truncate {
    /** \brief The argument value. */
    T value;
    /** \brief Construct by wrapping a value. */
    explicit truncate(T arg) : value(arg) {}
};

} // namespace tcspc::arg
