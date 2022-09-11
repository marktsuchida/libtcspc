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
 * Incoming events of type \c EventType are mapped to \c
 * DatapointEvent<DataType>. \c EventType and \c DataType are deduced from the
 * type of \c M (the data mapper).
 *
 * The data mapper must define member types \c EventType and \c DataType, plus
 * the function call operator <tt>DataType operator()(EventType const &) const
 * noexcept</tt>.
 *
 * \c DataType can be any copyable type accepted by the bin mapper used
 * downstream. Typically it is a numeric type.
 *
 * All other events are passed through.
 *
 * \see DifftimeDataMapper
 *
 * \tparam M type of data mapper
 * \tparam D downstream processor type
 */
template <typename M, typename D> class MapToDatapoints {
    M const mapper;
    D downstream;

  public:
    /**
     * \brief Type of events being mapped to datapoints.
     */
    using EventType = typename M::EventType;

    /**
     * \brief Data type of the produced datapoints.
     */
    using DataType = typename M::DataType;

    /**
     * \brief Construct with data mapper and downstream processor.
     *
     * \param mapper the data mapper (moved out)
     * \param downstream downstream processor (moved out)
     */
    explicit MapToDatapoints(M &&mapper, D &&downstream)
        : mapper(std::move(mapper)), downstream(std::move(downstream)) {}

    /** \brief Processor interface **/
    void HandleEvent(EventType const &event) noexcept {
        DatapointEvent<DataType> e{event.macrotime,
                                   std::invoke(mapper, event)};
        downstream.HandleEvent(e);
    }

    /** \brief Processor interface **/
    template <typename E> void HandleEvent(E const &event) noexcept {
        downstream.HandleEvent(event);
    }

    /** \brief Processor interface **/
    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
    }
};

/**
 * \brief Data mapper mapping difference time to the data value.
 *
 * \see MapToDatapoints
 */
class DifftimeDataMapper {
  public:
    /** \brief Data mapper interface */
    using EventType = TimeCorrelatedCountEvent;
    /** \brief Data mapper interface */
    using DataType = decltype(std::declval<EventType>().difftime);

    /** \brief Data mapper interface */
    DataType operator()(EventType const &event) const noexcept {
        return event.difftime;
    }
};

/**
 * \brief Processor that maps datapoints to histogram bin indices.
 *
 * Incoming events of type \c DatapointEvent<DataType> are mapped to \c
 * BinIncrementEvent<BinIndexType>. \c DataType and \c BinIndexType are deduced
 * from the type of \c M (the bin mapper).
 *
 * The bin mapper must define member types \c DataType and \c BinIndexType,
 * plus the function call operator <tt>std::optional<BinIndexType>
 * operator()(DataType) const noexcept</tt>.
 *
 * Bin mappers should also implement <tt>std::size_t GetNBins() const
 * noexcept</tt>.
 *
 * \c BinIndexType must be an unsigned integer type.
 *
 * All other events are passed through.
 *
 * \see LinearBinMapper
 * \see PowerOf2BinMapper
 *
 * \tparam M type of bin mapper
 * \tparam D downstream processor type
 */
template <typename M, typename D> class MapToBins {
    M const binMapper;
    D downstream;

    static_assert(
        std::is_unsigned_v<typename M::BinIndexType>,
        "The bin mapper's BinIndexType must be an unsigned integer type");

  public:
    /**
     * \brief Data type of the mapped datapoint.
     */
    using DataType = typename M::DataType;

    /**
     * \brief Type used for histogram bin indices.
     */
    using BinIndexType = typename M::BinIndexType;

    /**
     * \brief Construct with bin mapper and downstream processor.
     *
     * \param binMapper the bin mapper (moved out)
     * \param downstream downstream processor (moved out)
     */
    explicit MapToBins(M &&binMapper, D &&downstream)
        : binMapper(std::move(binMapper)), downstream(std::move(downstream)) {}

    /** \brief Processor interface **/
    void HandleEvent(DatapointEvent<DataType> const &event) noexcept {
        auto bin = std::invoke(binMapper, event.value);
        if (bin) {
            BinIncrementEvent<BinIndexType> e{event.macrotime, bin.value()};
            downstream.HandleEvent(e);
        }
    }

    /** \brief Processor interface **/
    template <typename E> void HandleEvent(E const &event) noexcept {
        downstream.HandleEvent(event);
    }

    /** \brief Processor interface **/
    void HandleEnd(std::exception_ptr error) noexcept {
        downstream.HandleEnd(error);
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
 * \see MapToBins
 *
 * \tparam TData the data type from which to map
 * \tparam TBinIndex the type used for bin indices
 * \tparam NDataBits number of significant bits in the datapoints
 * \tparam NHistoBits number of bits used for bin indices
 * \tparam Flip whether to flip the bin indices
 */
template <typename TData, typename TBinIndex, unsigned NDataBits,
          unsigned NHistoBits, bool Flip = false>
class PowerOf2BinMapper {
    static_assert(std::is_unsigned_v<TData>,
                  "TData must be an unsigned integer type");
    static_assert(std::is_unsigned_v<TBinIndex>,
                  "TBinIndex must be an unsigned integer type");

  public:
    /** \brief Bin mapper interface */
    using DataType = TData;
    /** \brief Bin mapper interface */
    using BinIndexType = TBinIndex;

    /** \brief Bin mapper interface */
    std::size_t GetNBins() const noexcept {
        return std::size_t{1} << NHistoBits;
    }

    /** \brief Bin mapper interface */
    std::optional<BinIndexType> operator()(DataType d) const noexcept {
        static_assert(sizeof(DataType) >= sizeof(BinIndexType));
        static_assert(NDataBits <= 8 * sizeof(DataType));
        static_assert(NHistoBits <= 8 * sizeof(BinIndexType));
        static_assert(NDataBits >= NHistoBits);
        constexpr int shift = NDataBits - NHistoBits;
        auto bin = [&] {
            if constexpr (shift >= 8 * sizeof(DataType))
                return 0;
            else
                return d >> shift;
        }();
        constexpr DataType maxBinIndex = (1 << NHistoBits) - 1;
        if (bin > maxBinIndex)
            return std::nullopt;
        if constexpr (Flip)
            bin = maxBinIndex - bin;
        return BinIndexType(bin);
    }
};

/**
 * \brief Bin mapper for linear histograms of arbitrary size.
 *
 * \see MapToBins
 *
 * \tparam TData the data type from which to map
 * \tparam TBinIndex the type used for bin indices
 */
template <typename TData, typename TBinIndex> class LinearBinMapper {
    TData const offset;
    TData const binWidth;
    TBinIndex const maxBinIndex;
    bool const clamp;

    static_assert(std::is_integral_v<TData>, "TData must be an integer type");
    static_assert(std::is_unsigned_v<TBinIndex>,
                  "TBinIndex must be an unsigned integer type");

  public:
    /** \brief Bin mapper interface */
    using DataType = TData;
    /** \brief Bin mapper interface */
    using BinIndexType = TBinIndex;

    /**
     * \brief Construct with parameters.
     *
     * <tt>(binCount - 1)</tt> must be in the range of \c BinIndexType.
     *
     * A negative \c binWidth value (together with a positive \c offset value)
     * can be used to flip the histogram, provided that \c DataType is a signed
     * type with sufficient range.
     *
     * \param offset minimum value mapped to the first bin
     * \param binWidth width of each bin (in datapoint units); must not be 0
     * \param maxBinIndex number of bins minus one (must not be negative)
     * \param clamp if true, include datapoints outside of the mapped range in
     * the first and last bins
     */
    explicit LinearBinMapper(DataType offset, DataType binWidth,
                             BinIndexType maxBinIndex, bool clamp = false)
        : offset(offset), binWidth(binWidth), maxBinIndex(maxBinIndex),
          clamp(clamp) {
        assert(binWidth != 0);
    }

    /** \brief Bin mapper interface */
    std::size_t GetNBins() const noexcept {
        return std::size_t(maxBinIndex) + 1;
    }

    /** \brief Bin mapper interface */
    std::optional<BinIndexType> operator()(DataType d) const noexcept {
        d -= offset;
        // Check sign before dividing to avoid rounding to zero in division.
        if ((d < 0 && binWidth > 0) || (d > 0 && binWidth < 0))
            return clamp ? std::make_optional(BinIndexType{0}) : std::nullopt;
        d /= binWidth;
        if (std::uint64_t(d) > maxBinIndex)
            return clamp ? std::make_optional(maxBinIndex) : std::nullopt;
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
class BatchBinIncrements {
    bool inBatch = false;
    BinIncrementBatchEvent<TBinIndex> batch;

    D downstream;

  public:
    /**
     * \brief Construct with downstream processor.
     *
     * \param downstream downstream processor (moved out)
     */
    explicit BatchBinIncrements(D &&downstream) : downstream(downstream) {}

    /** \brief Processor interface **/
    void HandleEvent(BinIncrementEvent<TBinIndex> const &event) noexcept {
        if (inBatch)
            batch.binIndices.push_back(event.binIndex);
    }

    /** \brief Processor interface **/
    void HandleEvent(EStart const &event) noexcept {
        batch.binIndices.clear();
        inBatch = true;
        batch.start = event.macrotime;
    }

    /** \brief Processor interface **/
    void HandleEvent(EStop const &event) noexcept {
        if (inBatch) {
            batch.stop = event.macrotime;
            downstream.HandleEvent(batch);
            inBatch = false;
        }
    }

    /** \brief Processor interface **/
    template <typename E> void HandleEvent(E const &event) noexcept {
        downstream.HandleEvent(event);
    }

    /** \brief Processor interface **/
    void HandleEnd(std::exception_ptr error) noexcept {
        batch.binIndices.clear();
        batch.binIndices.shrink_to_fit();
        downstream.HandleEnd(error);
    }
};

} // namespace flimevt
