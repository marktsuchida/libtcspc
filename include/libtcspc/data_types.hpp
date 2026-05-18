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
 * \brief The default data type set.
 *
 * This data type set is the default for the `DataTypes` template parameter in
 * most events, processors, and auxiliary objects that require a data type set.
 *
 * Custom data type sets may (but need not) derive from this type and override
 * some or all of the member type aliases.
 *
 * \ingroup data-types
 */
struct default_data_types {
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
 * \brief Utility for naming a data type set without additional definitions.
 *
 * \ingroup data-types
 *
 * This is intended for use by code generators, such as the Python bindings.
 *
 * In normal C++ code, it is better to define a data type set from scratch, or
 * by deriving from `tcspc::default_data_types`. Use of this template should be
 * avoided because the template parameter ordering is error-prone.
 */
template <typename Abstime = default_data_types::abstime_type,
          typename Channel = default_data_types::channel_type,
          typename Difftime = default_data_types::difftime_type,
          typename Count = default_data_types::count_type,
          typename Datapoint = default_data_types::datapoint_type,
          typename BinIndex = default_data_types::bin_index_type,
          typename Bin = default_data_types::bin_type>
struct parameterized_data_types {
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
 * \brief Concept that is satisfied when \p DT provides an integral
 * `abstime_type`.
 *
 * \ingroup data-types
 *
 * \note A satisfied concept indicates that \p DT names an integral
 * `abstime_type`, provided that `DT` can be instantiated (if \p DT is a
 * template class).
 */
template <typename DT>
concept with_abstime_type = std::integral<typename DT::abstime_type>;

/**
 * \brief Concept that is satisfied when \p DT provides an integral
 * `channel_type`.
 *
 * \ingroup data-types
 *
 * \note A satisfied concept indicates that \p DT names an integral
 * `channel_type`, provided that `DT` can be instantiated (if \p DT is a
 * template class).
 */
template <typename DT>
concept with_channel_type = std::integral<typename DT::channel_type>;

/**
 * \brief Concept that is satisfied when \p DT provides an integral
 * `difftime_type`.
 *
 * \ingroup data-types
 *
 * \note A satisfied concept indicates that \p DT names an integral
 * `difftime_type`, provided that `DT` can be instantiated (if \p DT is a
 * template class).
 */
template <typename DT>
concept with_difftime_type = std::integral<typename DT::difftime_type>;

/**
 * \brief Concept that is satisfied when \p DT provides an integral
 * `count_type`.
 *
 * \ingroup data-types
 *
 * \note A satisfied concept indicates that \p DT names an integral
 * `count_type`, provided that `DT` can be instantiated (if \p DT is a template
 * class).
 */
template <typename DT>
concept with_count_type = std::integral<typename DT::count_type>;

/**
 * \brief Concept that is satisfied when \p DT provides an integral
 * `datapoint_type`.
 *
 * \ingroup data-types
 *
 * \note A satisfied concept indicates that \p DT names an integral
 * `datapoint_type`, provided that `DT` can be instantiated (if \p DT is a
 * template class).
 */
template <typename DT>
concept with_datapoint_type = std::integral<typename DT::datapoint_type>;

/**
 * \brief Concept that is satisfied when \p DT provides an integral
 * `bin_index_type`.
 *
 * \ingroup data-types
 *
 * \note A satisfied concept indicates that \p DT names an integral
 * `bin_index_type`, provided that `DT` can be instantiated (if \p DT is a
 * template class).
 */
template <typename DT>
concept with_bin_index_type = std::integral<typename DT::bin_index_type>;

/**
 * \brief Concept that is satisfied when \p DT provides an integral
 * `bin_type`.
 *
 * \ingroup data-types
 *
 * \note A satisfied concept indicates that \p DT names an integral `bin_type`,
 * provided that `DT` can be instantiated (if \p DT is a template class).
 */
template <typename DT>
concept with_bin_type = std::integral<typename DT::bin_type>;

} // namespace tcspc
