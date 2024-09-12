/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/bin_increment_cluster_encoding.hpp"

#include "libtcspc/int_types.hpp"
#include "libtcspc/span.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <trompeloeil/mock.hpp>

#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

namespace tcspc::internal {

// Test with signed element type (i8), which is more bug-prone than unsigned
// due to the signedness conversion during encoding/decoding.

struct mock_storage {
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MAKE_MOCK0(available_capacity, std::size_t());
    MAKE_MOCK1(make_space, span<i8>(std::size_t));
    // NOLINTEND(modernize-use-trailing-return-type)
};

struct ref_storage {
    mock_storage *stor;

    explicit ref_storage(mock_storage &impl) : stor(&impl) {}

    [[nodiscard]] auto available_capacity() const -> std::size_t {
        return stor->available_capacity();
    }

    [[nodiscard]] auto make_space(std::size_t size) const -> span<i8> {
        return stor->make_space(size);
    }
};

TEST_CASE("encode_bin_increment_cluster encodes empty") {
    auto storage = mock_storage();
    ALLOW_CALL(storage, available_capacity()).RETURN(1);
    std::vector<i8> encoded(1);
    ALLOW_CALL(storage, make_space(1)).LR_RETURN(span(encoded));
    CHECK(
        encode_bin_increment_cluster(ref_storage{storage}, span<i8 const>()));
    CHECK(encoded == std::vector<i8>{0});
}

TEST_CASE("encode_bin_increment_cluster rejects empty when no space") {
    auto storage = mock_storage();
    ALLOW_CALL(storage, available_capacity()).RETURN(0);
    CHECK_FALSE(
        encode_bin_increment_cluster(ref_storage{storage}, span<i8 const>()));
}

TEST_CASE("encode_bin_increment_cluster encodes small") {
    auto storage = mock_storage();
    auto const cluster = std::vector<i8>{1, 2, 3};
    ALLOW_CALL(storage, available_capacity()).RETURN(4);
    std::vector<i8> encoded(4);
    ALLOW_CALL(storage, make_space(4)).LR_RETURN(span(encoded));
    CHECK(encode_bin_increment_cluster(ref_storage{storage},
                                       span(std::as_const(cluster))));
    CHECK(encoded == std::vector<i8>{3, 1, 2, 3});
}

TEST_CASE("encode_bin_increment_cluster rejects when space insufficient") {
    auto storage = mock_storage();
    auto const cluster = std::vector<i8>{1, 2, 3};
    ALLOW_CALL(storage, available_capacity()).RETURN(3);
    CHECK_FALSE(encode_bin_increment_cluster(ref_storage{storage},
                                             span(std::as_const(cluster))));
}

TEST_CASE("encode_bin_increment_cluster encodes large") {
    auto storage = mock_storage();
    auto const cluster = std::vector<i8>(255, 42);
    auto const encoded_size = 255 + 1 + sizeof(std::size_t);
    ALLOW_CALL(storage, available_capacity()).RETURN(encoded_size);
    std::vector<i8> encoded(encoded_size);
    ALLOW_CALL(storage, make_space(encoded_size)).LR_RETURN(span(encoded));
    CHECK(encode_bin_increment_cluster(ref_storage{storage}, span(cluster)));
    CHECK(static_cast<u8>(encoded[0]) == 255);
    std::size_t written_size{};
    std::memcpy(&written_size, &encoded[1], sizeof(written_size));
    CHECK(written_size == 255);
    CHECK(encoded[1 + sizeof(std::size_t)] == 42);
    CHECK(encoded.back() == 42);
}

TEST_CASE("encode_bin_increment_cluster appends to storage") {
    auto storage = mock_storage();
    auto const cluster = std::vector<i8>{1, 2, 3};
    ALLOW_CALL(storage, available_capacity()).RETURN(4);
    std::vector<i8> encoded{3, -1, -2, -3, 42, 42, 42, 42};
    ALLOW_CALL(storage, make_space(4)).LR_RETURN(span(encoded).subspan(4));
    CHECK(encode_bin_increment_cluster(ref_storage{storage},
                                       span(std::as_const(cluster))));
    CHECK(encoded == std::vector<i8>{3, -1, -2, -3, 3, 1, 2, 3});
}

TEST_CASE("bin_increment_cluster_decoder decodes empty") {
    std::vector<i8> const encoded;
    auto const decoder = bin_increment_cluster_decoder(span(encoded));
    CHECK(decoder.begin() == decoder.end());
}

TEST_CASE("bin_increment_cluster_decoder decodes small") {
    std::vector<i8> const encoded{3, 1, 2, 3};
    auto const decoder = bin_increment_cluster_decoder(span(encoded));

    auto it = decoder.begin();
    span<i8 const> const cluster = *it;
    CHECK(cluster.size() == 3);
    CHECK(std::vector<i8>(cluster.begin(), cluster.end()) ==
          std::vector<i8>{1, 2, 3});

    ++it;
    CHECK(it == decoder.end());
}

TEST_CASE("bin_increment_cluster_decoder decodes large") {
    std::vector<i8> encoded{static_cast<i8>(u8(255))};
    encoded.insert(encoded.end(), sizeof(std::size_t), 42);
    std::size_t encoded_size = 255;
    std::memcpy(&encoded[1], &encoded_size, sizeof(encoded_size));
    encoded.insert(encoded.end(), 255, 123);
    REQUIRE(encoded.size() == 1 + sizeof(std::size_t) + 255);

    auto const decoder = bin_increment_cluster_decoder(span(encoded));

    auto it = decoder.begin();
    span<i8 const> const cluster = *it;
    CHECK(cluster.size() == 255);
    CHECK(int(cluster.front()) == 123);
    CHECK(int(cluster.back()) == 123);

    ++it;
    CHECK(it == decoder.end());
}

} // namespace tcspc::internal
