/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "Common.hpp"
#include "HistogramEvents.hpp"
#include "TimeTaggedEvents.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace flimevt {

/**
 * \brief Processor that maps arbitrary timestamped events to datapoint events.
 *
 * Incoming events of type \c event_type are mapped to \c
 * datapoint_event<data_type>. \c event_type and \c data_type are deduced from
 * the type of \c M (the data mapper).
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
 * \see difftime_data_mapper
 *
 * \tparam M type of data mapper
 * \tparam D downstream processor type
 */
template <typename M, typename D> class map_to_datapoints {
    M const mapper;
    D downstream;

  public:
    /**
     * \brief Type of events being mapped to datapoints.
     */
    using event_type = typename M::event_type;

    /**
     * \brief Data type of the produced datapoints.
     */
    using data_type = typename M::data_type;

    /**
     * \brief Construct with data mapper and downstream processor.
     *
     * \param mapper the data mapper (moved out)
     * \param downstream downstream processor (moved out)
     */
    explicit map_to_datapoints(M &&mapper, D &&downstream)
        : mapper(std::move(mapper)), downstream(std::move(downstream)) {}

    /** \brief Processor interface **/
    void handle_event(event_type const &event) noexcept {
        datapoint_event<data_type> e{event.macrotime,
                                     std::invoke(mapper, event)};
        downstream.handle_event(e);
    }

    /** \brief Processor interface **/
    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    /** \brief Processor interface **/
    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

/**
 * \brief Data mapper mapping difference time to the data value.
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
    data_type operator()(event_type const &event) const noexcept {
        return event.difftime;
    }
};

/**
 * \brief Processor that maps datapoints to histogram bin indices.
 *
 * Incoming events of type \c datapoint_event<data_type> are mapped to \c
 * bin_increment_event<bin_index_type>. \c data_type and \c bin_index_type are
 * deduced from the type of \c M (the bin mapper).
 *
 * The bin mapper must define member types \c data_type and \c bin_index_type,
 * plus the function call operator <tt>std::optional<bin_index_type>
 * operator()(data_type) const noexcept</tt>.
 *
 * Bin mappers should also implement <tt>std::size_t get_n_bins() const
 * noexcept</tt>.
 *
 * \c bin_index_type must be an unsigned integer type.
 *
 * All other events are passed through.
 *
 * \see linear_bin_mapper
 * \see power_of_2_bin_mapper
 *
 * \tparam M type of bin mapper
 * \tparam D downstream processor type
 */
template <typename M, typename D> class map_to_bins {
    M const bin_mapper;
    D downstream;

    static_assert(
        std::is_unsigned_v<typename M::bin_index_type>,
        "The bin mapper's bin_index_type must be an unsigned integer type");

  public:
    /**
     * \brief Data type of the mapped datapoint.
     */
    using data_type = typename M::data_type;

    /**
     * \brief Type used for histogram bin indices.
     */
    using bin_index_type = typename M::bin_index_type;

    /**
     * \brief Construct with bin mapper and downstream processor.
     *
     * \param bin_mapper the bin mapper (moved out)
     * \param downstream downstream processor (moved out)
     */
    explicit map_to_bins(M &&bin_mapper, D &&downstream)
        : bin_mapper(std::move(bin_mapper)),
          downstream(std::move(downstream)) {}

    /** \brief Processor interface **/
    void handle_event(datapoint_event<data_type> const &event) noexcept {
        auto bin = std::invoke(bin_mapper, event.value);
        if (bin) {
            bin_increment_event<bin_index_type> e{event.macrotime,
                                                  bin.value()};
            downstream.handle_event(e);
        }
    }

    /** \brief Processor interface **/
    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    /** \brief Processor interface **/
    void handle_end(std::exception_ptr error) noexcept {
        downstream.handle_end(error);
    }
};

/**
 * \brief Bin mapper that discards the least significant bits.
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
 * \tparam TData the data type from which to map
 * \tparam TBinIndex the type used for bin indices
 * \tparam NDataBits number of significant bits in the datapoints
 * \tparam NHistoBits number of bits used for bin indices
 * \tparam Flip whether to flip the bin indices
 */
template <typename TData, typename TBinIndex, unsigned NDataBits,
          unsigned NHistoBits, bool Flip = false>
class power_of_2_bin_mapper {
    static_assert(std::is_unsigned_v<TData>,
                  "TData must be an unsigned integer type");
    static_assert(std::is_unsigned_v<TBinIndex>,
                  "TBinIndex must be an unsigned integer type");

  public:
    /** \brief Bin mapper interface */
    using data_type = TData;
    /** \brief Bin mapper interface */
    using bin_index_type = TBinIndex;

    /** \brief Bin mapper interface */
    std::size_t get_n_bins() const noexcept {
        return std::size_t{1} << NHistoBits;
    }

    /** \brief Bin mapper interface */
    std::optional<bin_index_type> operator()(data_type d) const noexcept {
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
 * \see map_to_bins
 *
 * \tparam TData the data type from which to map
 * \tparam TBinIndex the type used for bin indices
 */
template <typename TData, typename TBinIndex> class linear_bin_mapper {
    TData const offset;
    TData const bin_width;
    TBinIndex const max_bin_index;
    bool const clamp;

    static_assert(std::is_integral_v<TData>, "TData must be an integer type");
    static_assert(std::is_unsigned_v<TBinIndex>,
                  "TBinIndex must be an unsigned integer type");

  public:
    /** \brief Bin mapper interface */
    using data_type = TData;
    /** \brief Bin mapper interface */
    using bin_index_type = TBinIndex;

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
     * \param bin_width width of each bin (in datapoint units); must not be 0
     * \param max_bin_index number of bins minus one (must not be negative)
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
    std::size_t get_n_bins() const noexcept {
        return std::size_t(max_bin_index) + 1;
    }

    /** \brief Bin mapper interface */
    std::optional<bin_index_type> operator()(data_type d) const noexcept {
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

/**
 * \brief Processor collecting binned data into batches.
 *
 * \tparam TBinIndex the bin index type
 * \tparam EStart start-of-batch event type
 * \tparam EStop end-of-batch event type
 * \tparam D downstream processor type
 */
template <typename TBinIndex, typename EStart, typename EStop, typename D>
class batch_bin_increments {
    bool in_batch = false;
    bin_increment_batch_event<TBinIndex> batch;

    D downstream;

  public:
    /**
     * \brief Construct with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit batch_bin_increments(D &&downstream) : downstream(downstream) {}

    /** \brief Processor interface **/
    void handle_event(bin_increment_event<TBinIndex> const &event) noexcept {
        if (in_batch)
            batch.binIndices.push_back(event.binIndex);
    }

    /** \brief Processor interface **/
    void handle_event(EStart const &event) noexcept {
        batch.binIndices.clear();
        in_batch = true;
        batch.start = event.macrotime;
    }

    /** \brief Processor interface **/
    void handle_event(EStop const &event) noexcept {
        if (in_batch) {
            batch.stop = event.macrotime;
            downstream.handle_event(batch);
            in_batch = false;
        }
    }

    /** \brief Processor interface **/
    template <typename E> void handle_event(E const &event) noexcept {
        downstream.handle_event(event);
    }

    /** \brief Processor interface **/
    void handle_end(std::exception_ptr error) noexcept {
        batch.binIndices.clear();
        batch.binIndices.shrink_to_fit();
        downstream.handle_end(error);
    }
};

} // namespace flimevt
