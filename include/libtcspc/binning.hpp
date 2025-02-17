/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "context.hpp"
#include "data_types.hpp"
#include "histogram_events.hpp"
#include "int_arith.hpp"
#include "int_types.hpp"
#include "introspect.hpp"
#include "processor_traits.hpp"
#include "span.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tcspc {

namespace internal {

template <typename Event, typename DataTypes, typename DataMapper,
          typename Downstream>
class map_to_datapoints {
    static_assert(std::is_same_v<std::invoke_result_t<DataMapper, Event>,
                                 typename DataTypes::datapoint_type>);

    static_assert(is_processor_v<Downstream, datapoint_event<DataTypes>>);

    DataMapper mapper;

    Downstream downstream;

  public:
    explicit map_to_datapoints(DataMapper mapper, Downstream downstream)
        : mapper(std::move(mapper)), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "map_to_datapoints");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    void handle(Event const &event) {
        downstream.handle(
            datapoint_event<DataTypes>{std::invoke(mapper, event)});
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(Event &&event) { handle(static_cast<Event const &>(event)); }

    template <typename OtherEvent,
              typename = std::enable_if_t<
                  handles_event_v<Downstream, remove_cvref_t<OtherEvent>>>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that maps arbitrary time-tagged events to
 * datapoint events.
 *
 * \ingroup processors-binning
 *
 * Incoming events of type \p Event are mapped to `tcspc::datapoint_event`s
 * according to \p DataMapper (see \ref data-mappers).
 *
 * All other events are passed through.
 *
 * \tparam Event event type to map to datapoints
 *
 * \tparam DataTypes data type set for emitted events
 *
 * \tparam DataMapper type of data mapper (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param mapper the data mapper
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `Event`: map to datapoint with data mapper and emit as
 *   `tcspc::datapoint_event<DataTypes>`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename Event, typename DataTypes = default_data_types,
          typename DataMapper, typename Downstream>
auto map_to_datapoints(DataMapper mapper, Downstream downstream) {
    return internal::map_to_datapoints<Event, DataTypes, DataMapper,
                                       Downstream>(std::move(mapper),
                                                   std::move(downstream));
}

/**
 * \brief Data mapper mapping difference time to the data value.
 *
 * \ingroup data-mappers
 *
 * The event being mapped must have a `difftime` field.
 *
 * \tparam DataTypes data type set specifying `datapoint_type`
 */
template <typename DataTypes = default_data_types> class difftime_data_mapper {
  public:
    /** \brief Implements data mapper requirement */
    template <typename Event>
    auto operator()(Event const &event) const ->
        typename DataTypes::datapoint_type {
        static_assert(
            internal::is_type_in_range<typename DataTypes::datapoint_type>(
                decltype(event.difftime){0}),
            "difftime_data_mapper does not allow narrowing conversion");
        return event.difftime;
    }
};

/**
 * \brief Data mapper mapping count to the data value.
 *
 * \ingroup data-mappers
 *
 * The event being mapped must have a `count` field.
 *
 * \tparam DataTypes data type set specifying `datapoint_type`
 */
template <typename DataTypes = default_data_types> class count_data_mapper {
  public:
    /** \brief Implements data mapper requirement */
    template <typename Event>
    auto operator()(Event const &event) const ->
        typename DataTypes::datapoint_type {
        static_assert(
            internal::is_type_in_range<typename DataTypes::datapoint_type>(
                decltype(event.count){0}),
            "count_data_mapper does not allow narrowing conversion");
        return event.count;
    }
};

/**
 * \brief Data mapper mapping channel to the data value.
 *
 * \ingroup data-mappers
 *
 * The event being mapped must have a `channel` field.
 *
 * \tparam DataTypes data type set specifying `datapoint_type`
 */
template <typename DataTypes = default_data_types> class channel_data_mapper {
  public:
    /** \brief Implements data mapper requirement */
    template <typename Event>
    auto operator()(Event const &event) const ->
        typename DataTypes::datapoint_type {
        static_assert(
            internal::is_type_in_range<typename DataTypes::datapoint_type>(
                decltype(event.channel){0}),
            "channel_data_mapper does not allow narrowing conversion");
        return event.channel;
    }
};

namespace internal {

template <typename DataTypes, typename BinMapper, typename Downstream>
class map_to_bins {
    static_assert(is_processor_v<Downstream, bin_increment_event<DataTypes>>);

    static_assert(
        std::is_same_v<std::invoke_result_t<
                           BinMapper, typename DataTypes::datapoint_type>,
                       std::optional<typename DataTypes::bin_index_type>>);

    BinMapper bin_mapper;
    Downstream downstream;

    static_assert(std::is_unsigned_v<typename DataTypes::bin_index_type>,
                  "The bin_index_type must be an unsigned integer type");

  public:
    explicit map_to_bins(BinMapper bin_mapper, Downstream downstream)
        : bin_mapper(std::move(bin_mapper)),
          downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "map_to_bins");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT> void handle(datapoint_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::datapoint_type,
                                     typename DataTypes::datapoint_type>);
        auto bin = std::invoke(bin_mapper, event.value);
        if (bin)
            downstream.handle(bin_increment_event<DataTypes>{bin.value()});
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    template <typename DT> void handle(datapoint_event<DT> &&event) {
        handle(static_cast<datapoint_event<DT> const &>(event));
    }

    template <typename OtherEvent,
              typename = std::enable_if_t<
                  handles_event_v<Downstream, remove_cvref_t<OtherEvent>>>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that maps datapoints to histogram bin indices.
 *
 * \ingroup processors-binning
 *
 * Incoming `tcspc::datapoint_event`s are mapped to
 * `tcspc::bin_increment_event`s according to \p BinMapper (see \ref
 * bin-mappers).
 *
 * All other events are passed through.
 *
 * \tparam DataTypes data type set for emitted events
 *
 * \tparam BinMapper type of bin mapper (usually deduced)
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param bin_mapper the bin mapper
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `tcspc::datapoint_event<DT>`: map to bin index with bin mapper; if not
 *   discarded by the bin mapper, emit as
 *   `tcspc::bin_increment_event<DataTypes>`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename BinMapper,
          typename Downstream>
auto map_to_bins(BinMapper bin_mapper, Downstream downstream) {
    return internal::map_to_bins<DataTypes, BinMapper, Downstream>(
        std::move(bin_mapper), std::move(downstream));
}

/**
 * \brief Bin mapper that discards the least significant bits.
 *
 * \ingroup bin-mappers
 *
 * This bin mapper performs fast linear binning by taking the most significant
 * bits of the datapoint as the bin index.
 *
 * For example, if \p NDataBits is 12 and \p NHistoBits is 8, incoming
 * datapoints must contain values in the range [0, 4095] and will be mapped to
 * bin indices [0, 255], where each bin has a width of 16.
 *
 * No division operations are used by this bin mapper.
 *
 * Datapoints outside of the mapped range are discarded.
 *
 * \tparam NDataBits number of significant bits in the datapoints
 *
 * \tparam NHistoBits number of bits used for bin indices
 *
 * \tparam Flip whether to flip the bin indices
 *
 * \tparam DataTypes data type set specifying `datapoint_type` and
 * `bin_index_type`
 */
template <unsigned NDataBits, unsigned NHistoBits, bool Flip = false,
          typename DataTypes = default_data_types>
struct power_of_2_bin_mapper {
    static_assert(NHistoBits <= 64); // Assumption made below.
    static_assert(NDataBits >= NHistoBits);
    static_assert(NDataBits <= 8 * sizeof(typename DataTypes::datapoint_type));
    static_assert(NHistoBits <=
                  8 * sizeof(typename DataTypes::bin_index_type));
    // Bin indices are always non-negative.
    static_assert(std::is_unsigned_v<typename DataTypes::bin_index_type> ||
                  NHistoBits < 8 * sizeof(typename DataTypes::bin_index_type));

    /** \brief Implements bin mapper requirement. */
    [[nodiscard]] auto n_bins() const -> std::size_t {
        return std::size_t{1} << NHistoBits;
    }

    /** \brief Implements bin mapper requirement. */
    auto operator()(typename DataTypes::datapoint_type datapoint) const
        -> std::optional<typename DataTypes::bin_index_type> {
        using datapoint_type = typename DataTypes::datapoint_type;
        using bin_index_type = typename DataTypes::bin_index_type;

        static constexpr int shift = NDataBits - NHistoBits;
        static_assert(shift <= 8 * sizeof(datapoint_type)); // Ensured above.
        if constexpr (shift == 8 * sizeof(datapoint_type))
            return bin_index_type{0}; // Shift would be UB.

        // Convert to unsigned (if necessary) _after_ the shift so that
        // negative datapoints are mapped to indices above max.
        auto const shifted = internal::ensure_unsigned(datapoint >> shift);
        static constexpr auto max_bin_index = static_cast<bin_index_type>(
            NHistoBits == 64 ? std::numeric_limits<u64>::max()
                             : (u64(1) << NHistoBits) - 1);
        if (shifted > max_bin_index)
            return std::nullopt;

        auto const bin_index = static_cast<bin_index_type>(shifted);
        if constexpr (Flip)
            return static_cast<bin_index_type>(max_bin_index - bin_index);
        else
            return bin_index;
    }
};

/**
 * \brief Bin mapper for linear histograms of arbitrary size.
 *
 * \ingroup bin-mappers
 *
 * \tparam DataTypes data type set specifying `datapoint_type` and
 * `bin_index_type`
 */
template <typename DataTypes = default_data_types> class linear_bin_mapper {
    typename DataTypes::datapoint_type off;
    typename DataTypes::datapoint_type bwidth;
    typename DataTypes::bin_index_type max_index;
    bool clamp;

    static_assert(std::is_integral_v<typename DataTypes::datapoint_type>,
                  "datapoint_type must be an integer type");
    static_assert(std::is_integral_v<typename DataTypes::bin_index_type>,
                  "bin_index_type must be an integer type");

    // Assumptions used by implementation.
    static_assert(sizeof(typename DataTypes::datapoint_type) <= sizeof(u64));
    static_assert(sizeof(typename DataTypes::bin_index_type) <= sizeof(u64));

  public:
    /**
     * \brief Construct with parameters.
     *
     * \p max_bin_index must be in the range of \p bin_index_type.
     *
     * A negative \p bin_width value (together with a positive \p offset value)
     * can be used to flip the histogram, provided that `datapoint_type` is a
     * signed type with sufficient range.
     *
     * \param offset minimum value mapped to the first bin
     *
     * \param bin_width width of each bin (in datapoint units); must not be 0
     *
     * \param max_bin_index number of bins minus one (must not be negative)
     *
     * \param clamp if true, include datapoints outside of the mapped range in
     * the first and last bins
     */
    explicit linear_bin_mapper(
        arg::offset<typename DataTypes::datapoint_type> offset,
        arg::bin_width<typename DataTypes::datapoint_type> bin_width,
        arg::max_bin_index<typename DataTypes::bin_index_type> max_bin_index,
        arg::clamp<bool> clamp = arg::clamp{false})
        : off(offset.value), bwidth(bin_width.value),
          max_index(max_bin_index.value), clamp(clamp.value) {
        if (bwidth == 0)
            throw std::invalid_argument(
                "linear_bin_mapper bin_width must not be zero");
        if (max_index < 0)
            throw std::invalid_argument(
                "linear_bin_mapper max_bin_index must not be negative");
    }

    /** \brief Implements bin mapper requirement. */
    [[nodiscard]] auto n_bins() const -> std::size_t {
        return std::size_t(max_index) + 1;
    }

    /** \brief Implements bin mapper requirement. */
    auto operator()(typename DataTypes::datapoint_type datapoint) const
        -> std::optional<typename DataTypes::bin_index_type> {
        using bin_index_type = typename DataTypes::bin_index_type;

        if (bwidth < 0 ? datapoint > off : datapoint < off)
            return clamp ? std::make_optional(bin_index_type{0})
                         : std::nullopt;
        // Note we always divide non-negative by positive or non-positive by
        // negative, to avoid being affected by truncation toward zero.
        auto const scaled = (datapoint - off) / bwidth;
        assert(scaled >= 0);
        if (u64(scaled) > u64(max_index))
            return clamp ? std::make_optional(max_index) : std::nullopt;
        return static_cast<bin_index_type>(scaled);
    }
};

/**
 * \brief Access for `tcspc::unique_bin_mapper` data.
 *
 * \ingroup context-access
 */
template <typename T> class unique_bin_mapper_access {
    std::function<std::vector<T>()> values_fn;

  public:
    /** \private */
    template <typename Func>
    explicit unique_bin_mapper_access(Func values_func)
        : values_fn(values_func) {}

    /**
     * \brief Return the datapoint values assigned to bin indices.
     */
    auto values() -> std::vector<T> { return values_fn(); }
};

/**
 * \brief Bin mapper that maps unique datapoints to consecutive bin indices.
 *
 * \ingroup bin-mappers
 *
 * This is intended for use with datapoints that only have a small number of
 * unique values (for example, those from `tcspc::channel_data_mapper`).
 *
 * Each datapoint value is mapped to a bin index starting from 0, assigned in
 * the order in which the value is encountered.
 *
 * The datapoint values for each bin index can later be retrieved via the
 * context.
 *
 * \tparam DataTypes data type set specifying `datapoint_type` and
 * `bin_index_type`.
 */
template <typename DataTypes = default_data_types> class unique_bin_mapper {
    using datapoint_type = typename DataTypes::datapoint_type;
    using bin_index_type = typename DataTypes::bin_index_type;
    bin_index_type max_index;
    std::vector<datapoint_type> values;
    access_tracker<unique_bin_mapper_access<datapoint_type>> trk;

  public:
    /**
     * \brief Construct with context and parameter.
     *
     * \param tracker access tracker for later access of the datapoint values
     *
     * \param max_bin_index number of bins minus one (must not be negative)
     */
    explicit unique_bin_mapper(
        access_tracker<
            unique_bin_mapper_access<typename DataTypes::datapoint_type>>
            &&tracker,
        arg::max_bin_index<typename DataTypes::bin_index_type> max_bin_index)
        : max_index(max_bin_index.value), trk(std::move(tracker)) {
        if (max_index < 0)
            throw std::invalid_argument(
                "unique_bin_mapper max_bin_index must not be negative");
        trk.register_access_factory([](auto &tracker) {
            auto *self =
                LIBTCSPC_OBJECT_FROM_TRACKER(unique_bin_mapper, trk, tracker);
            return unique_bin_mapper_access<datapoint_type>(
                [self] { return self->values; });
        });
    }

    /** \brief Implements bin mapper requirement. */
    [[nodiscard]] auto n_bins() const -> std::size_t {
        return std::size_t(max_index) + 1;
    }

    /** \brief Implements bin mapper requirement. */
    auto operator()(typename DataTypes::datapoint_type datapoint)
        -> std::optional<typename DataTypes::bin_index_type> {
        auto it = std::find(values.begin(), values.end(), datapoint);
        if (it == values.end()) {
            values.push_back(datapoint);
            it = std::prev(values.end());
        }
        auto idx = std::distance(values.begin(), it);
        return u64(idx) <= u64(max_index)
                   ? std::make_optional(bin_index_type(idx))
                   : std::nullopt;
    }
};

namespace internal {

template <typename StartEvent, typename StopEvent, typename DataTypes,
          typename Downstream>
class cluster_bin_increments {
    static_assert(
        is_processor_v<Downstream, bin_increment_cluster_event<DataTypes>>);

    bool in_cluster = false;
    std::vector<typename DataTypes::bin_index_type> cur_cluster;

    Downstream downstream;

  public:
    explicit cluster_bin_increments(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "cluster_bin_increments");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT> void handle(bin_increment_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTypes::bin_index_type>);
        if (in_cluster)
            cur_cluster.push_back(event.bin_index);
    }

    void handle(StartEvent const & /* event */) {
        cur_cluster.clear();
        in_cluster = true;
    }

    void handle(StopEvent const & /* event */) {
        if (in_cluster) {
            auto const e = bin_increment_cluster_event<DataTypes>{
                ad_hoc_bucket(span(cur_cluster))};
            downstream.handle(e);
            in_cluster = false;
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    template <typename DT> void handle(bin_increment_event<DT> &&event) {
        handle(static_cast<bin_increment_event<DT> const &>(event));
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(StartEvent &&event) {
        handle(static_cast<StartEvent const &>(event));
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    void handle(StopEvent &&event) {
        handle(static_cast<StopEvent const &>(event));
    }

    template <typename OtherEvent,
              typename = std::enable_if_t<
                  handles_event_v<Downstream, remove_cvref_t<OtherEvent>>>>
    void handle(OtherEvent &&event) {
        downstream.handle(std::forward<OtherEvent>(event));
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor collecting binned data into clusters.
 *
 * \ingroup processors-binning
 *
 * \tparam StartEvent start-of-cluster event type
 *
 * \tparam StopEvent end-of-cluster event type
 *
 * \tparam DataTypes data type set specifying `bin_index_type` and emitted
 * events
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `StartEvent`: discard any unfinished cluster; start recording a cluster
 * - `StopEvent`: ignore if not in cluster; finish recording the current
 *   cluster and emit as `tcspc::bin_increment_cluster_event<DataTypes>`
 * - `tcspc::bin_increment_event<DT>`: record if currently within a cluster
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename StartEvent, typename StopEvent,
          typename DataTypes = default_data_types, typename Downstream>
auto cluster_bin_increments(Downstream downstream) {
    return internal::cluster_bin_increments<StartEvent, StopEvent, DataTypes,
                                            Downstream>(std::move(downstream));
}

} // namespace tcspc
