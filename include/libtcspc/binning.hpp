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

template <typename DataTraits, typename DataMapper, typename Downstream>
class map_to_datapoints {
    DataMapper mapper;
    Downstream downstream;

  public:
    using event_type = typename DataMapper::event_type;
    using datapoint_type = typename DataMapper::datapoint_type;
    static_assert(
        std::is_same_v<datapoint_type, typename DataTraits::datapoint_type>);

    explicit map_to_datapoints(DataMapper mapper, Downstream downstream)
        : mapper(std::move(mapper)), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "map_to_datapoints");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    void handle(event_type const &event) {
        downstream.handle(datapoint_event<DataTraits>{
            event.abstime, std::invoke(mapper, event)});
    }

    template <typename OtherEvent> void handle(OtherEvent const &event) {
        downstream.handle(event);
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that maps arbitrary timestamped events to
 * datapoint events.
 *
 * \ingroup processors-histogram
 *
 * Incoming events of type \c event_type are mapped to \c
 * datapoint_event<DataTraits>. \c event_type and \c datapoint_type are deduced
 * from the type of \c DataMapper (the data mapper).
 *
 * The data mapper must define member types \c event_type and \c
 * datapoint_type, plus the function call operator <tt>datapoint_type
 * operator()(event_type const &) const</tt>.
 *
 * \c datapoint_type can be any copyable type accepted by the bin mapper used
 * downstream. Typically it is a numeric type. It must match \c
 * DataTraits::datapoint_type.
 *
 * All other events are passed through.
 *
 * \tparam DataTraits traits type for emitted events
 *
 * \tparam DataMapper type of data mapper
 *
 * \tparam Downstream downstream processor type
 *
 * \param mapper the data mapper (moved out)
 *
 * \param downstream downstream processor (moved out)
 *
 * \return map-to-datapoints processor
 */
template <typename DataTraits = default_data_traits, typename DataMapper,
          typename Downstream>
auto map_to_datapoints(DataMapper &&mapper, Downstream &&downstream) {
    return internal::map_to_datapoints<DataTraits, DataMapper, Downstream>(
        std::forward<DataMapper>(mapper),
        std::forward<Downstream>(downstream));
}

/**
 * \brief Data mapper mapping difference time to the data value.
 *
 * \ingroup data-mappers
 *
 * \see map_to_datapoints
 *
 * \tparam Event event type to map (must have difftime field)
 */
template <typename Event = time_correlated_detection_event<>>
class difftime_data_mapper {
  public:
    /** \brief Data mapper interface */
    using event_type = Event;
    /** \brief Data mapper interface */
    using datapoint_type = decltype(std::declval<event_type>().difftime);
    static_assert(std::is_integral_v<datapoint_type>);

    /** \brief Data mapper interface */
    auto operator()(event_type const &event) const -> datapoint_type {
        return event.difftime;
    }
};

/**
 * \brief Data mapper mapping count to the data value.
 *
 * \ingroup data-mappers
 *
 * \see map_to_datapoints
 *
 * \tparam Event event type to map (must have count field)
 */
template <typename Event = nontagged_counts_event<>> class count_data_mapper {
  public:
    /** \brief Data mapper interface */
    using event_type = Event;
    /** \brief Data mapper interface */
    using datapoint_type = decltype(std::declval<event_type>().count);
    static_assert(std::is_integral_v<datapoint_type>);

    /** \brief Data mapper interface */
    auto operator()(event_type const &event) const -> datapoint_type {
        return event.count;
    }
};

namespace internal {

template <typename DataTraits, typename BinMapper, typename Downstream>
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
        std::is_same_v<bin_index_type, typename DataTraits::bin_index_type>);

    explicit map_to_bins(BinMapper bin_mapper, Downstream downstream)
        : bin_mapper(std::move(bin_mapper)),
          downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "map_to_bins");
        return info;
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
            downstream.handle(
                bin_increment_event<DataTraits>{event.abstime, bin.value()});
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
 * \ingroup processors-histogram
 *
 * Incoming events of type \c datapoint_event<datapoint_type> are mapped to \c
 * bin_increment_event<bin_index_type>. \c datapoint_type and \c bin_index_type
 * are deduced from the type of \c BinMapper (the bin mapper).
 *
 * The bin mapper must define member types \c datapoint_type and \c
 * bin_index_type, plus the function call operator
 * <tt>std::optional<bin_index_type> operator()(datapoint_type) const</tt>.
 *
 * Bin mappers should also implement <tt>std::size_t n_bins() const</tt>.
 *
 * \c bin_index_type must be an unsigned integer type.
 *
 * All other events are passed through.
 *
 * \see linear_bin_mapper
 *
 * \see power_of_2_bin_mapper
 *
 * \tparam DataTraits traits type for emitted events
 *
 * \tparam BinMapper type of bin mapper
 *
 * \tparam Downstream downstream processor type
 *
 * \param bin_mapper the bin mapper (moved out)
 *
 * \param downstream downstream processor (moved out)
 *
 * \return map-to-bin processor
 */
template <typename DataTraits = default_data_traits, typename BinMapper,
          typename Downstream>
auto map_to_bins(BinMapper &&bin_mapper, Downstream &&downstream) {
    return internal::map_to_bins<DataTraits, BinMapper, Downstream>(
        std::forward<BinMapper>(bin_mapper),
        std::forward<Downstream>(downstream));
}

/**
 * \brief Bin mapper that discards the least significant bits.
 *
 * \ingroup bin-mapper
 *
 * This bin mapper performs fast linear binning by taking the most significant
 * bits of the datapoint as the bin index.
 *
 * For example, if \c NDataBits is 12 and \c NHistoBits is 8, incoming
 * datapoints must contain values in the range [0, 4095] and will be mapped to
 * bin indices [0, 255], where each bin has a width of 16.
 *
 * No division operations are used by this bin mapper.
 *
 * Datapoints outside of the mapped range are discarded.
 *
 * \see map_to_bins
 *
 * \tparam NDataBits number of significant bits in the datapoints
 *
 * \tparam NHistoBits number of bits used for bin indices
 *
 * \tparam Flip whether to flip the bin indices
 *
 * \tparam DataTraits traits type specifying \c datapoint_type and \c
 * bin_index_type
 */
template <unsigned NDataBits, unsigned NHistoBits, bool Flip = false,
          typename DataTraits = default_data_traits>
struct power_of_2_bin_mapper {
    /** \brief Bin mapper interface */
    using datapoint_type = typename DataTraits::datapoint_type;
    static_assert((std::is_unsigned_v<datapoint_type> &&
                   NDataBits <= 8 * sizeof(datapoint_type)) ||
                  NDataBits <= 8 * sizeof(datapoint_type) - 1);

    /** \brief Bin mapper interface */
    using bin_index_type = typename DataTraits::bin_index_type;
    static_assert((std::is_unsigned_v<bin_index_type> &&
                   NHistoBits <= 8 * sizeof(bin_index_type)) ||
                  NHistoBits <= 8 * sizeof(bin_index_type) - 1);

    /** \brief Bin mapper interface */
    [[nodiscard]] auto n_bins() const -> std::size_t {
        return std::size_t{1} << NHistoBits;
    }

    /** \brief Bin mapper interface */
    auto operator()(datapoint_type d) const -> std::optional<bin_index_type> {
        static_assert(sizeof(datapoint_type) >= sizeof(bin_index_type));
        static_assert(NDataBits <= 8 * sizeof(datapoint_type));
        static_assert(NHistoBits <= 8 * sizeof(bin_index_type));
        static_assert(NDataBits >= NHistoBits);
        constexpr int shift = NDataBits - NHistoBits;
        auto bin = [&] {
            if constexpr (shift >= 8 * sizeof(datapoint_type))
                return 0;
            else
                return d >> shift;
        }();
        constexpr datapoint_type max_bin_index = (1 << NHistoBits) - 1;
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
 * \ingroup bin-mapper
 *
 * \see map_to_bins
 *
 * \tparam DataTraits traits type specifying \c datapoint_type and \c
 * bin_index_type
 */
template <typename DataTraits = default_data_traits> class linear_bin_mapper {
    typename DataTraits::datapoint_type off;
    typename DataTraits::datapoint_type bwidth;
    typename DataTraits::bin_index_type max_index;
    bool clamp;

    static_assert(std::is_integral_v<typename DataTraits::datapoint_type>,
                  "datapoint_type must be an integer type");
    static_assert(std::is_unsigned_v<typename DataTraits::bin_index_type>,
                  "bin_index_type must be an unsigned integer type");

  public:
    /** \brief Bin mapper interface */
    using datapoint_type = typename DataTraits::datapoint_type;
    /** \brief Bin mapper interface */
    using bin_index_type = typename DataTraits::bin_index_type;

    /**
     * \brief Construct with parameters.
     *
     * \c max_bin_index must be in the range of \c bin_index_type.
     *
     * A negative \c bin_width value (together with a positive \c offset value)
     * can be used to flip the histogram, provided that \c datapoint_type is a
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

    /** \brief Bin mapper interface */
    [[nodiscard]] auto n_bins() const -> std::size_t {
        return std::size_t(max_index) + 1;
    }

    /** \brief Bin mapper interface */
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

template <typename StartEvent, typename StopEvent, typename DataTraits,
          typename Downstream>
class batch_bin_increments {
    bool in_batch = false;
    bin_increment_batch_event<DataTraits> batch;

    Downstream downstream;

  public:
    explicit batch_bin_increments(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        processor_info info(this, "batch_bin_increments");
        return info;
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        auto g = downstream.introspect_graph();
        g.push_entry_point(this);
        return g;
    }

    template <typename DT> void handle(bin_increment_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTraits::bin_index_type>);
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
 * \ingroup processors-histogram
 *
 * \tparam StartEvent start-of-batch event type
 *
 * \tparam StopEvent end-of-batch event type
 *
 * \tparam DataTraits traits type specifying \c bin_index_type and used for
 * emitted events
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return batch-bin-increments processor
 */
template <typename StartEvent, typename StopEvent,
          typename DataTraits = default_data_traits, typename Downstream>
auto batch_bin_increments(Downstream &&downstream) {
    return internal::batch_bin_increments<StartEvent, StopEvent, DataTraits,
                                          Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
