/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "int_types.hpp"

#include <concepts>

namespace tcspc {

/**
 * \brief The default numeric traits.
 *
 * These numeric traits are the default for the `NumericTraits` template
 * parameter in most events, processors, and auxiliary objects that require
 * numeric traits.
 *
 * Custom numeric traits may (but need not) derive from this type and override
 * some or all of the member type aliases.
 *
 * \ingroup numeric-traits
 */
struct default_numeric_traits {
    /**
     * \brief Absolute time type.
     *
     * The default of `i64` is chosen because 64-bit precision is reasonable
     * (32-bit would overflow; 128-bit would hurt performance and is not
     * required for most applications) and because we want to allow negative
     * time stamps.
     */
    using abstime_type = i64;

    /**
     * \brief Channel number type.
     */
    using channel_type = i32;

    /**
     * \brief Difference time type.
     */
    using difftime_type = i32;

    /**
     * \brief Count type.
     *
     * This is used in events carrying a count of detections.
     */
    using count_type = u32;

    /**
     * \brief Type of datapoint for histogramming.
     */
    using datapoint_type = i32;

    /**
     * \brief Type of histogram bin index.
     */
    using bin_index_type = u16;

    /**
     * \brief Type of histogram bin value (count).
     */
    using bin_type = u16;
};

/**
 * \brief Utility for naming numeric traits without additional definitions.
 *
 * \ingroup numeric-traits
 *
 * This is intended for use by code generators, such as the Python bindings.
 *
 * In normal C++ code, it is better to define numeric traits from scratch, or
 * by deriving from `tcspc::default_numeric_traits`. Use of this template
 * should be avoided because the template parameter ordering is error-prone.
 */
template <typename Abstime = default_numeric_traits::abstime_type,
          typename Channel = default_numeric_traits::channel_type,
          typename Difftime = default_numeric_traits::difftime_type,
          typename Count = default_numeric_traits::count_type,
          typename Datapoint = default_numeric_traits::datapoint_type,
          typename BinIndex = default_numeric_traits::bin_index_type,
          typename Bin = default_numeric_traits::bin_type>
struct parameterized_numeric_traits {
    /** \cond hidden-from-docs */
    using abstime_type = Abstime;
    using channel_type = Channel;
    using difftime_type = Difftime;
    using count_type = Count;
    using datapoint_type = Datapoint;
    using bin_index_type = BinIndex;
    using bin_type = Bin;
    /** \endcond */
};

/**
 * \brief Concept that is satisfied when \p NT provides an integral
 * `abstime_type`.
 *
 * \ingroup numeric-traits
 *
 * \note A satisfied concept indicates that \p NT names an integral
 * `abstime_type`, provided that `NT` can be instantiated (if \p NT is a
 * template class).
 */
template <typename NT>
concept with_abstime_type = std::integral<typename NT::abstime_type>;

/**
 * \brief Concept that is satisfied when \p NT provides an integral
 * `channel_type`.
 *
 * \ingroup numeric-traits
 *
 * \note A satisfied concept indicates that \p NT names an integral
 * `channel_type`, provided that `NT` can be instantiated (if \p NT is a
 * template class).
 */
template <typename NT>
concept with_channel_type = std::integral<typename NT::channel_type>;

/**
 * \brief Concept that is satisfied when \p NT provides an integral
 * `difftime_type`.
 *
 * \ingroup numeric-traits
 *
 * \note A satisfied concept indicates that \p NT names an integral
 * `difftime_type`, provided that `NT` can be instantiated (if \p NT is a
 * template class).
 */
template <typename NT>
concept with_difftime_type = std::integral<typename NT::difftime_type>;

/**
 * \brief Concept that is satisfied when \p NT provides an integral
 * `count_type`.
 *
 * \ingroup numeric-traits
 *
 * \note A satisfied concept indicates that \p NT names an integral
 * `count_type`, provided that `NT` can be instantiated (if \p NT is a template
 * class).
 */
template <typename NT>
concept with_count_type = std::integral<typename NT::count_type>;

/**
 * \brief Concept that is satisfied when \p NT provides an integral
 * `datapoint_type`.
 *
 * \ingroup numeric-traits
 *
 * \note A satisfied concept indicates that \p NT names an integral
 * `datapoint_type`, provided that `NT` can be instantiated (if \p NT is a
 * template class).
 */
template <typename NT>
concept with_datapoint_type = std::integral<typename NT::datapoint_type>;

/**
 * \brief Concept that is satisfied when \p NT provides an integral
 * `bin_index_type`.
 *
 * \ingroup numeric-traits
 *
 * \note A satisfied concept indicates that \p NT names an integral
 * `bin_index_type`, provided that `NT` can be instantiated (if \p NT is a
 * template class).
 */
template <typename NT>
concept with_bin_index_type = std::integral<typename NT::bin_index_type>;

/**
 * \brief Concept that is satisfied when \p NT provides an integral
 * `bin_type`.
 *
 * \ingroup numeric-traits
 *
 * \note A satisfied concept indicates that \p NT names an integral `bin_type`,
 * provided that `NT` can be instantiated (if \p NT is a template class).
 */
template <typename NT>
concept with_bin_type = std::integral<typename NT::bin_type>;

} // namespace tcspc
