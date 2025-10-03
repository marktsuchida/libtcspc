/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "span.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

namespace tcspc::internal {

// Implementation of encoding/decoding for use by batch_bin_increment_clusters
// and unbatch_bin_increment_clusters.
//
// Encode bin increment clusters in a single stream as follows.
// - The stream element type E (a signed or unsigned integer) is equal to the
//   bin index type.
// - Each cluster is prefixed with its size as follows. Let UE be the unsigned
//   integer type corresponding to E.
//   - If the cluster size is less than the maximum value of UE, it is stored
//     as a single stream element.
//   - Otherwise a single stream element containing the maximum value of UE is
//     stored, followed by sizeof(std::size_t) unaligned bytes containing the
//     size.
// - The cluster's bin indices are stored in order following the size prefix.

template <typename BinIndex> struct bin_increment_cluster_encoding_traits {
    using encoded_size_type = std::make_unsigned_t<BinIndex>;
    static_assert(sizeof(encoded_size_type) <= sizeof(std::size_t));
    static constexpr auto encoded_size_max =
        std::numeric_limits<encoded_size_type>::max();
    static constexpr auto large_size_element_count =
        sizeof(std::size_t) / sizeof(encoded_size_type);
};

template <typename BinIndex>
[[nodiscard]] constexpr auto
encoded_bin_increment_cluster_size(std::size_t cluster_size) noexcept
    -> std::size_t {
    using traits = bin_increment_cluster_encoding_traits<BinIndex>;
    bool const is_long_mode = cluster_size >= traits::encoded_size_max;
    std::size_t const size_of_size =
        is_long_mode ? 1 + traits::large_size_element_count : 1;
    return size_of_size + cluster_size;
}

// The Storage type must define the following member functions:
// - [[nodiscard]] auto available_capacity() const -> std::size_t;
// - [[nodiscard]] auto make_space(std::size_t) -> span<BinIndex>;
// The latter is only called when capacity is available.
//
// Returns true if the encoded cluster fit in storge; false if not, in which
// case storage is not modified.
template <typename BinIndex, typename Storage>
[[nodiscard]] auto encode_bin_increment_cluster(Storage dest,
                                                span<BinIndex const> cluster)
    -> bool {
    using traits = bin_increment_cluster_encoding_traits<BinIndex>;

    std::size_t const size = cluster.size();
    bool const is_long_mode = size >= traits::encoded_size_max;
    std::size_t const size_of_size =
        is_long_mode ? 1 + traits::large_size_element_count : 1;
    std::size_t const total_size = size_of_size + size;
    if (total_size > dest.available_capacity())
        return false;

    auto spn = dest.make_space(total_size);
    if (is_long_mode) {
        spn.front() = static_cast<BinIndex>(traits::encoded_size_max);
        spn = spn.subspan(1);
        auto const size_dest = as_writable_bytes(
            spn.subspan(0, traits::large_size_element_count));
        assert(size_dest.size() == sizeof(size));
        std::memcpy(size_dest.data(), &size, sizeof(size));
        spn = spn.subspan(size_dest.size());
    } else {
        spn.front() = static_cast<BinIndex>(
            static_cast<typename traits::encoded_size_type>(size));
        spn = spn.subspan(1);
    }
    assert(spn.size() == cluster.size());
    std::copy(cluster.begin(), cluster.end(), spn.begin());
    return true;
}

template <typename BinIndex> class bin_increment_cluster_decoder {
    using traits = bin_increment_cluster_encoding_traits<BinIndex>;

    span<BinIndex const> clusters;

  public:
    explicit bin_increment_cluster_decoder(span<BinIndex const> clusters)
        : clusters(clusters) {}

    // Constant forward iterator over the clusters. There is no non-const
    // iteration support. Dereferencing yields `span<BinIndex const>`
    // (including for empty clusters).
    class const_iterator {
        typename span<BinIndex const>::iterator it;

        // Given a valid (not past-end) it, return the range of the encoded
        // cluster pointed to by *this.
        auto cluster_range() const -> std::pair<decltype(it), decltype(it)> {
            bool const is_long_mode =
                static_cast<typename traits::encoded_size_type>(*it) ==
                traits::encoded_size_max;
            std::size_t const size_of_size =
                is_long_mode ? 1 + traits::large_size_element_count : 1;
            std::size_t const cluster_size =
                is_long_mode
                    ? std::invoke([sit = std::next(it)] {
                          std::size_t size{};
                          std::memcpy(&size, &*sit, sizeof(size));
                          return size;
                      })
                    : static_cast<typename traits::encoded_size_type>(*it);
            auto const start =
                std::next(it, static_cast<std::ptrdiff_t>(size_of_size));
            auto const stop =
                std::next(start, static_cast<std::ptrdiff_t>(cluster_size));
            return {start, stop};
        }

        explicit const_iterator(decltype(it) iter) : it(iter) {}

        friend class bin_increment_cluster_decoder;

      public:
        using value_type = span<BinIndex const>;
        using difference_type = std::ptrdiff_t;
        using reference = value_type const &;
        using pointer = value_type const *;
        using iterator_category = std::input_iterator_tag;

        const_iterator() = delete;

        auto operator++() -> const_iterator & {
            auto const [start, stop] = cluster_range();
            it = stop;
            return *this;
        }

        auto operator++(int) -> const_iterator {
            auto const ret = *this;
            ++(*this);
            return ret;
        }

        auto operator*() const -> value_type {
            auto const [start, stop] = cluster_range();
            return span(&*start, &*stop);
        }

        auto operator==(const_iterator rhs) const noexcept -> bool {
            return it == rhs.it;
        }

        auto operator!=(const_iterator rhs) const noexcept -> bool {
            return not(*this == rhs);
        }
    };

    [[nodiscard]] auto begin() const noexcept -> const_iterator {
        return const_iterator(clusters.begin());
    }

    [[nodiscard]] auto end() const noexcept -> const_iterator {
        return const_iterator(clusters.end());
    }
};

// Deduction guide for span-of-non-const.
template <typename BinIndex>
bin_increment_cluster_decoder(span<BinIndex>)
    -> bin_increment_cluster_decoder<BinIndex>;

} // namespace tcspc::internal
