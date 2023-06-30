/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"
#include "histogram_events.hpp"
#include "time_tagged_events.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

template <typename DataMapper, typename Downstream> class map_to_datapoints {
    DataMapper mapper;
    Downstream downstream;

  public:
    using event_type = typename DataMapper::event_type;
    using data_type = typename DataMapper::data_type;

    explicit map_to_datapoints(DataMapper &&mapper, Downstream &&downstream)
        : mapper(std::move(mapper)), downstream(std::move(downstream)) {}

    void handle_event(event_type const &event) noexcept {
        datapoint_event<data_type> e{event.macrotime,
                                     std::invoke(mapper, event)};
        downstream.handle_event(e);
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that maps arbitrary timestamped events to
 * datapoint events.
 *
 * \ingroup processors
 *
 * Incoming events of type \c event_type are mapped to \c
 * datapoint_event<data_type>. \c event_type and \c data_type are deduced from
 * the type of \c DataMapper (the data mapper).
 *
 * The data mapper must define member types \c event_type and \c data_type,
 * plus the function call operator <tt>data_type operator()(event_type const &)
 * const noexcept</tt>.
 *
 * \c data_type can be any copyable type accepted by the bin mapper used
 * downstream. Typically it is a numeric type.
 *
 * All other events are passed through.
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
template <typename DataMapper, typename Downstream>
auto map_to_datapoints(DataMapper &&mapper, Downstream &&downstream) {
    return internal::map_to_datapoints<DataMapper, Downstream>(
        std::forward<DataMapper>(mapper),
        std::forward<Downstream>(downstream));
}

/**
 * \brief Data mapper mapping difference time to the data value.
 *
 * \ingroup data-mappers
 *
 * \see map_to_datapoints
 */
class difftime_data_mapper {
  public:
    /** \brief Data mapper interface */
    using event_type = time_correlated_count_event;
    /** \brief Data mapper interface */
    using data_type = decltype(std::declval<event_type>().difftime);

    /** \brief Data mapper interface */
    auto operator()(event_type const &event) const noexcept -> data_type {
        return event.difftime;
    }
};

namespace internal {

template <typename BinMapper, typename Downstream> class map_to_bins {
    BinMapper bin_mapper;
    Downstream downstream;

    static_assert(
        std::is_unsigned_v<typename BinMapper::bin_index_type>,
        "The bin mapper's bin_index_type must be an unsigned integer type");

  public:
    using data_type = typename BinMapper::data_type;
    using bin_index_type = typename BinMapper::bin_index_type;

    explicit map_to_bins(BinMapper &&bin_mapper, Downstream &&downstream)
        : bin_mapper(std::move(bin_mapper)),
          downstream(std::move(downstream)) {}

    void handle_event(datapoint_event<data_type> const &event) noexcept {
        auto bin = std::invoke(bin_mapper, event.value);
        if (bin) {
            bin_increment_event<bin_index_type> e{event.macrotime,
                                                  bin.value()};
            downstream.handle_event(e);
        }
    }

    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    void handle_end(std::exception_ptr const &error) noexcept {
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor that maps datapoints to histogram bin indices.
 *
 * \ingroup processors
 *
 * Incoming events of type \c datapoint_event<data_type> are mapped to \c
 * bin_increment_event<bin_index_type>. \c data_type and \c bin_index_type are
 * deduced from the type of \c BinMapper (the bin mapper).
 *
 * The bin mapper must define member types \c data_type and \c bin_index_type,
 * plus the function call operator <tt>std::optional<bin_index_type>
 * operator()(data_type) const noexcept</tt>.
 *
 * Bin mappers should also implement <tt>std::size_t n_bins() const
 * noexcept</tt>.
 *
 * \c bin_index_type must be an unsigned integer type.
 *
 * All other events are passed through.
 *
 * \see linear_bin_mapper
 *
 * \see power_of_2_bin_mapper
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
template <typename BinMapper, typename Downstream>
auto map_to_bins(BinMapper &&bin_mapper, Downstream &&downstream) {
    return internal::map_to_bins<BinMapper, Downstream>(
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
 * \tparam DataPoint the data type from which to map
 *
 * \tparam BinIndex the type used for bin indices
 *
 * \tparam NDataBits number of significant bits in the datapoints
 *
 * \tparam NHistoBits number of bits used for bin indices
 *
 * \tparam Flip whether to flip the bin indices
 */
template <typename DataPoint, typename BinIndex, unsigned NDataBits,
          unsigned NHistoBits, bool Flip = false>
class power_of_2_bin_mapper {
    static_assert(std::is_unsigned_v<DataPoint>,
                  "DataPoint must be an unsigned integer type");
    static_assert(std::is_unsigned_v<BinIndex>,
                  "BinIndex must be an unsigned integer type");

  public:
    /** \brief Bin mapper interface */
    using data_type = DataPoint;
    /** \brief Bin mapper interface */
    using bin_index_type = BinIndex;

    /** \brief Bin mapper interface */
    [[nodiscard]] auto n_bins() const noexcept -> std::size_t {
        return std::size_t{1} << NHistoBits;
    }

    /** \brief Bin mapper interface */
    auto operator()(data_type d) const noexcept
        -> std::optional<bin_index_type> {
        static_assert(sizeof(data_type) >= sizeof(bin_index_type));
        static_assert(NDataBits <= 8 * sizeof(data_type));
        static_assert(NHistoBits <= 8 * sizeof(bin_index_type));
        static_assert(NDataBits >= NHistoBits);
        constexpr int shift = NDataBits - NHistoBits;
        auto bin = [&] {
            if constexpr (shift >= 8 * sizeof(data_type))
                return 0;
            else
                return d >> shift;
        }();
        constexpr data_type max_bin_index = (1 << NHistoBits) - 1;
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
 * \tparam DataPoint the data type from which to map
 *
 * \tparam BinIndex the type used for bin indices
 */
template <typename DataPoint, typename BinIndex> class linear_bin_mapper {
    DataPoint offset;
    DataPoint bin_width;
    BinIndex max_bin_index;
    bool clamp;

    static_assert(std::is_integral_v<DataPoint>,
                  "DataPoint must be an integer type");
    static_assert(std::is_unsigned_v<BinIndex>,
                  "BinIndex must be an unsigned integer type");

  public:
    /** \brief Bin mapper interface */
    using data_type = DataPoint;
    /** \brief Bin mapper interface */
    using bin_index_type = BinIndex;

    /**
     * \brief Construct with parameters.
     *
     * \c max_bin_index must be in the range of \c bin_index_type.
     *
     * A negative \c bin_width value (together with a positive \c offset value)
     * can be used to flip the histogram, provided that \c data_type is a
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
    explicit linear_bin_mapper(data_type offset, data_type bin_width,
                               bin_index_type max_bin_index,
                               bool clamp = false)
        : offset(offset), bin_width(bin_width), max_bin_index(max_bin_index),
          clamp(clamp) {
        assert(bin_width != 0);
    }

    /** \brief Bin mapper interface */
    [[nodiscard]] auto n_bins() const noexcept -> std::size_t {
        return std::size_t(max_bin_index) + 1;
    }

    /** \brief Bin mapper interface */
    auto operator()(data_type d) const noexcept
        -> std::optional<bin_index_type> {
        d -= offset;
        // Check sign before dividing to avoid rounding to zero in division.
        if ((d < 0 && bin_width > 0) || (d > 0 && bin_width < 0))
            return clamp ? std::make_optional(bin_index_type{0})
                         : std::nullopt;
        d /= bin_width;
        if (std::uint64_t(d) > max_bin_index)
            return clamp ? std::make_optional(max_bin_index) : std::nullopt;
        return d;
    }
};

namespace internal {

template <typename BinIndex, typename StartEvent, typename StopEvent,
          typename Downstream>
class batch_bin_increments {
    bool in_batch = false;
    bin_increment_batch_event<BinIndex> batch;

    Downstream downstream;

  public:
    /**
     * \brief Construct with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit batch_bin_increments(Downstream &&downstream)
        : downstream(downstream) {}

    /** \brief Processor interface **/
    void handle_event(bin_increment_event<BinIndex> const &event) noexcept {
        if (in_batch)
            batch.bin_indices.push_back(event.bin_index);
    }

    /** \brief Processor interface **/
    void handle_event(StartEvent const &event) noexcept {
        batch.bin_indices.clear();
        in_batch = true;
        batch.time_range.start = event.macrotime;
    }

    /** \brief Processor interface **/
    void handle_event(StopEvent const &event) noexcept {
        if (in_batch) {
            batch.time_range.stop = event.macrotime;
            downstream.handle_event(std::as_const(batch));
            in_batch = false;
        }
    }

    /** \brief Processor interface **/
    template <typename OtherEvent>
    void handle_event(OtherEvent const &event) noexcept {
        downstream.handle_event(event);
    }

    /** \brief Processor interface **/
    void handle_end(std::exception_ptr const &error) noexcept {
        batch.bin_indices.clear();
        batch.bin_indices.shrink_to_fit();
        downstream.handle_end(error);
    }
};

} // namespace internal

/**
 * \brief Create a processor collecting binned data into batches.
 *
 * \ingroup processor
 *
 * \tparam BinIndex the bin index type
 *
 * \tparam StartEvent start-of-batch event type
 *
 * \tparam StopEvent end-of-batch event type
 *
 * \tparam Downstream downstream processor type
 *
 * \param downstream downstream processor (moved out)
 *
 * \return batch-bin-increments processor
 */
template <typename BinIndex, typename StartEvent, typename StopEvent,
          typename Downstream>
auto batch_bin_increments(Downstream &&downstream) {
    return internal::batch_bin_increments<BinIndex, StartEvent, StopEvent,
                                          Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
