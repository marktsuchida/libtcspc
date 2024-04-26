/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "common.hpp"
#include "histogram_events.hpp"
#include "introspect.hpp"
#include "time_tagged_events.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename Event, typename DataTypes, typename DataMapper,
          typename Downstream>
class map_to_datapoints {
    static_assert(std::is_same_v<std::invoke_result_t<DataMapper, Event>,
                                 typename DataTypes::datapoint_type>);

    DataMapper mapper;

    Downstream downstream;

  public:
    explicit map_to_datapoints(DataMapper mapper, Downstream downstream)
        : mapper(std::move(mapper)), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "map_to_datapoints");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(Event const &event) {
        downstream.handle(
            datapoint_event<DataTypes>{std::invoke(mapper, event)});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
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
auto map_to_datapoints(DataMapper &&mapper, Downstream &&downstream) {
    return internal::map_to_datapoints<Event, DataTypes, DataMapper,
                                       Downstream>(
        std::forward<DataMapper>(mapper),
        std::forward<Downstream>(downstream));
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
        return event.count;
    }
};

namespace internal {

template <typename DataTypes, typename BinMapper, typename Downstream>
class map_to_bins {
    BinMapper bin_mapper;
    Downstream downstream;

    static_assert(
        std::is_unsigned_v<typename BinMapper::bin_index_type>,
        "The bin mapper's bin_index_type must be an unsigned integer type");

  public:
    using datapoint_type = typename BinMapper::datapoint_type;
    using bin_index_type = typename BinMapper::bin_index_type;
    static_assert(
        std::is_same_v<bin_index_type, typename DataTypes::bin_index_type>);

    explicit map_to_bins(BinMapper bin_mapper, Downstream downstream)
        : bin_mapper(std::move(bin_mapper)),
          downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "map_to_bins");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(datapoint_event<DT> const &event) {
        static_assert(
            std::is_same_v<typename DT::datapoint_type, datapoint_type>);
        auto bin = std::invoke(bin_mapper, event.value);
        if (bin)
            downstream.handle(bin_increment_event<DataTypes>{bin.value()});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
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
auto map_to_bins(BinMapper &&bin_mapper, Downstream &&downstream) {
    return internal::map_to_bins<DataTypes, BinMapper, Downstream>(
        std::forward<BinMapper>(bin_mapper),
        std::forward<Downstream>(downstream));
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
    /** \brief Implements bin mapper requirement. */
    using datapoint_type = typename DataTypes::datapoint_type;
    static_assert((std::is_unsigned_v<datapoint_type> &&
                   NDataBits <= 8 * sizeof(datapoint_type)) ||
                  NDataBits <= 8 * sizeof(datapoint_type) - 1);

    /** \brief Implements bin mapper requirement. */
    using bin_index_type = typename DataTypes::bin_index_type;
    static_assert((std::is_unsigned_v<bin_index_type> &&
                   NHistoBits <= 8 * sizeof(bin_index_type)) ||
                  NHistoBits <= 8 * sizeof(bin_index_type) - 1);

    /** \brief Implements bin mapper requirement. */
    [[nodiscard]] auto n_bins() const -> std::size_t {
        return std::size_t{1} << NHistoBits;
    }

    /** \brief Implements bin mapper requirement. */
    auto operator()(datapoint_type d) const -> std::optional<bin_index_type> {
        static_assert(sizeof(datapoint_type) >= sizeof(bin_index_type));
        static_assert(NDataBits <= 8 * sizeof(datapoint_type));
        static_assert(NHistoBits <= 8 * sizeof(bin_index_type));
        static_assert(NDataBits >= NHistoBits);
        static constexpr int shift = NDataBits - NHistoBits;
        auto bin = [&] {
            if constexpr (shift >= 8 * sizeof(datapoint_type))
                return 0;
            else
                return d >> shift;
        }();
        static constexpr datapoint_type max_bin_index = (1 << NHistoBits) - 1;
        if (bin > max_bin_index)
            return std::nullopt;
        if constexpr (Flip)
            bin = max_bin_index - bin;
        return bin_index_type(bin);
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
    static_assert(std::is_unsigned_v<typename DataTypes::bin_index_type>,
                  "bin_index_type must be an unsigned integer type");

  public:
    /** \brief Implements bin mapper requirement. */
    using datapoint_type = typename DataTypes::datapoint_type;
    /** \brief Implements bin mapper requirement. */
    using bin_index_type = typename DataTypes::bin_index_type;

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
    explicit linear_bin_mapper(arg_offset<datapoint_type> offset,
                               arg_bin_width<datapoint_type> bin_width,
                               arg_max_bin_index<bin_index_type> max_bin_index,
                               bool clamp = false)
        : off(offset.value), bwidth(bin_width.value),
          max_index(max_bin_index.value), clamp(clamp) {
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
    auto operator()(datapoint_type d) const -> std::optional<bin_index_type> {
        d -= off;
        // Check sign before dividing to avoid rounding to zero in division.
        if ((d < 0 && bwidth > 0) || (d > 0 && bwidth < 0))
            return clamp ? std::make_optional(bin_index_type{0})
                         : std::nullopt;
        d /= bwidth;
        if (std::uint64_t(d) > max_index)
            return clamp ? std::make_optional(max_index) : std::nullopt;
        return bin_index_type(d);
    }
};

namespace internal {

template <typename StartEvent, typename StopEvent, typename DataTypes,
          typename Downstream>
class batch_bin_increments {
    bool in_batch = false;
    bin_increment_batch_event<DataTypes> batch;

    Downstream downstream;

  public:
    explicit batch_bin_increments(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "batch_bin_increments");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(bin_increment_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTypes::bin_index_type>);
        if (in_batch)
            batch.bin_indices.push_back(event.bin_index);
    }

    void handle([[maybe_unused]] StartEvent const &event) {
        batch.bin_indices.clear();
        in_batch = true;
    }

    void handle([[maybe_unused]] StopEvent const &event) {
        if (in_batch) {
            downstream.handle(std::as_const(batch));
            in_batch = false;
        }
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor collecting binned data into batches.
 *
 * \ingroup processors-binning
 *
 * \tparam StartEvent start-of-batch event type
 *
 * \tparam StopEvent end-of-batch event type
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
 * - `StartEvent`: discard any unfinished batch; start recording a batch
 * - `StopEvent`: ignore if not in batch; finish recording the current batch
 *   and emit as `tcspc::bin_increment_batch_event<DataTypes>`
 * - `tcspc::bin_increment_event<DT>`: record if currently within a batch
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename StartEvent, typename StopEvent,
          typename DataTypes = default_data_types, typename Downstream>
auto batch_bin_increments(Downstream &&downstream) {
    return internal::batch_bin_increments<StartEvent, StopEvent, DataTypes,
                                          Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
