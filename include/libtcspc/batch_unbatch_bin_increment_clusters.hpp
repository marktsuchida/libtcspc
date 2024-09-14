/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "arg_wrappers.hpp"
#include "bin_increment_cluster_encoding.hpp"
#include "bucket.hpp"
#include "histogram_events.hpp"
#include "processor_traits.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace tcspc {

namespace internal {

// Helper for batch_bin_increment_clusters.
template <typename BinIndex>
class batch_bin_increment_clusters_encoding_adapter {
    bucket<BinIndex> *bkt;
    std::size_t *siz;

  public:
    explicit batch_bin_increment_clusters_encoding_adapter(
        bucket<BinIndex> &storage, std::size_t &usage)
        : bkt(&storage), siz(&usage) {}

    [[nodiscard]] auto available_capacity() const -> std::size_t {
        return bkt->size() - *siz;
    }

    [[nodiscard]] auto make_space(std::size_t size) -> span<BinIndex> {
        assert(size <= available_capacity());
        auto const old_size = *siz;
        *siz += size;
        return bkt->subspan(old_size, size);
    }
};

template <typename DataTypes, typename Downstream>
class batch_bin_increment_clusters {
    using bin_index_type = typename DataTypes::bin_index_type;
    static_assert(is_processor_v<Downstream, bucket<bin_index_type>>);

    std::shared_ptr<bucket_source<bin_index_type>> bsource;

    bucket<bin_index_type> cur_batch;
    std::size_t bucket_used_size = 0;
    std::size_t cur_batch_size = 0;
    std::size_t bkt_siz;
    std::size_t batch_siz;

    Downstream downstream;

    void emit_cur_batch() {
        if (cur_batch_size > 0) {
            cur_batch.shrink(0, bucket_used_size);
            downstream.handle(std::as_const(cur_batch));
        }
        cur_batch = {};
        bucket_used_size = 0;
        cur_batch_size = 0;
    }

  public:
    explicit batch_bin_increment_clusters(
        std::shared_ptr<bucket_source<typename DataTypes::bin_index_type>>
            buffer_provider,
        arg::bucket_size<std::size_t> bucket_size,
        arg::batch_size<std::size_t> batch_size, Downstream downstream)
        : bsource(std::move(buffer_provider)), bkt_siz(bucket_size.value),
          batch_siz(batch_size.value), downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "batch_bin_increment_clusters");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename DT>
    void handle(bin_increment_cluster_event<DT> const &event) {
        static_assert(std::is_same_v<typename DT::bin_index_type,
                                     typename DataTypes::bin_index_type>);
        if (cur_batch.empty())
            cur_batch = bsource->bucket_of_size(bkt_siz);
        bool const did_fit = encode_bin_increment_cluster(
            batch_bin_increment_clusters_encoding_adapter(cur_batch,
                                                          bucket_used_size),
            span(event.bin_indices));
        if (not did_fit) {
            emit_cur_batch();
            cur_batch = bsource->bucket_of_size(bkt_siz);
            bool const did_fit = encode_bin_increment_cluster(
                batch_bin_increment_clusters_encoding_adapter(
                    cur_batch, bucket_used_size),
                span(event.bin_indices));
            if (not did_fit)
                throw std::runtime_error(
                    "bin increment cluster does not fit in maximum batch size");
        }
        ++cur_batch_size;
        if (cur_batch_size == batch_siz)
            emit_cur_batch();
    }

    // NOLINTBEGIN(cppcoreguidelines-rvalue-reference-param-not-moved)
    template <typename DT>
    void handle(bin_increment_cluster_event<DT> &&event) {
        handle(static_cast<bin_increment_cluster_event<DT> const &>(event));
    }
    // NOLINTEND(cppcoreguidelines-rvalue-reference-param-not-moved)

    template <typename Event, typename = std::enable_if_t<handles_event_v<
                                  Downstream, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        downstream.handle(std::forward<Event>(event));
    }

    void flush() {
        emit_cur_batch();
        downstream.flush();
    }
};

template <typename DataTypes, typename Downstream>
class unbatch_bin_increment_clusters {
    using bin_index_type = typename DataTypes::bin_index_type;
    static_assert(
        is_processor_v<Downstream, bin_increment_cluster_event<DataTypes>>);

    Downstream downstream;

  public:
    explicit unbatch_bin_increment_clusters(Downstream downstream)
        : downstream(std::move(downstream)) {}

    [[nodiscard]] auto introspect_node() const -> processor_info {
        return processor_info(this, "unbatch_bin_increment_clusters");
    }

    [[nodiscard]] auto introspect_graph() const -> processor_graph {
        return downstream.introspect_graph().push_entry_point(this);
    }

    template <typename Event,
              typename = std::enable_if_t<
                  std::is_convertible_v<
                      typename std::iterator_traits<
                          decltype(std::declval<Event>().end())>::reference,
                      typename DataTypes::bin_index_type const &> ||
                  handles_event_v<Downstream, remove_cvref_t<Event>>>>
    void handle(Event &&event) {
        if constexpr (std::is_convertible_v<
                          typename std::iterator_traits<
                              decltype(std::declval<Event>()
                                           .end())>::reference,
                          bin_index_type const &>) {
            bin_increment_cluster_decoder<bin_index_type> const decoder(event);
            for (auto const cluster_span : decoder) {
                // The cluster_span is a span<T const>, but we want bucket<T>,
                // not bucket<T const>. Casting is safe because
                // `ad_hoc_bucket<T>` emitted as const lvalue reference does
                // not allow mutation of the referred data.
                auto const mut_span = span<bin_index_type>(
                    const_cast<bin_index_type *>(cluster_span.data()),
                    cluster_span.size());
                bin_increment_cluster_event<DataTypes> e{
                    ad_hoc_bucket(mut_span)};
                downstream.handle(std::as_const(e));
            }
        } else {
            downstream.handle(std::forward<Event>(event));
        }
    }

    void flush() { downstream.flush(); }
};

} // namespace internal

/**
 * \brief Create a processor that collects bin increment clusters into encoded
 * batches.
 *
 * \ingroup processors-batching
 *
 * This is an optimized analogue of `tcspc::batch()` for the specific case of
 * `tcspc::bin_increment_cluster_event`; it avoids allocating memory for each
 * cluster individually. It must be paired with
 * `tcspc::unbatch_bin_increment_clusters()`.
 *
 * The `bucket_size` must be large enough that all clusters fit in a single
 * bucket (including the encoded cluster size); otherwise an exception is
 * thrown.
 *
 * \tparam DataTypes data type set specifying `bin_index_type`
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param buffer_provider bucket source providing buffers for the batches
 *
 * \param bucket_size size of buckets to use; batches are emitted when the next
 * cluster will not fit in the bucket
 *
 * \param batch_size maximum number of clusters to include in a batch; if 0,
 * limit only by bucket size
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - `bin_increment_cluster_event<DataTypes>`: collect and encode into
 *   `tcspc::bucket<typename DataTypes::bin_index_type>` and emit as batch
 * - All other types: pass through with no action
 * - Flush: emit any buffered clusters as `tcspc::bucket<typename
 *   DataTypes::bin_index_type>`; pass through
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto batch_bin_increment_clusters(
    std::shared_ptr<bucket_source<typename DataTypes::bin_index_type>>
        buffer_provider,
    arg::bucket_size<std::size_t> bucket_size,
    arg::batch_size<std::size_t> batch_size, Downstream &&downstream) {
    return internal::batch_bin_increment_clusters<DataTypes, Downstream>(
        std::move(buffer_provider), bucket_size, batch_size,
        std::forward<Downstream>(downstream));
}

/**
 * \brief Create a processor that splits encoded batches of bin increment
 * clusters into indivisual clusters.
 *
 * \ingroup processors-batching
 *
 * This is an optimized analogue of `tcspc::unbatch()` for the specific case of
 * `tcspc::bin_increment_cluster_event`. It must be paired with
 * `tcspc::batch_bin_increment_clusters()`.
 *
 * \tparam DataTypes data type set specifying `bin_index_type` and the emitted
 * events
 *
 * \tparam Downstream downstream processor type (usually deduced)
 *
 * \param downstream downstream processor
 *
 * \return processor
 *
 * \par Events handled
 * - Range (container, iterable; typically bucket) of
 *   `DataTypes::bin_index_type`: decode and emit each cluster as
 *   `bin_increment_cluster_event<DataTypes>`
 * - All other types: pass through with no action
 * - Flush: pass through with no action
 */
template <typename DataTypes = default_data_types, typename Downstream>
auto unbatch_bin_increment_clusters(Downstream &&downstream) {
    return internal::unbatch_bin_increment_clusters<DataTypes, Downstream>(
        std::forward<Downstream>(downstream));
}

} // namespace tcspc
