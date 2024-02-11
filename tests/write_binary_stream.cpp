/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/write_binary_stream.hpp"

#include "libtcspc/buffer.hpp"
#include "libtcspc/own_on_copy_view.hpp"
#include "libtcspc/span.hpp"
#include "libtcspc/view_as_bytes.hpp"
#include "test_checkers.hpp"

// Trompeloeil requires catch2 to be included first, but does not define which
// subset of Catch2 3.x is required. So include catch_all.hpp.
#include <catch2/catch_all.hpp>
#include <catch2/trompeloeil.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <optional>
#include <vector>

namespace tcspc {

TEST_CASE("introspect write_binary_stream", "[introspect]") {
    check_introspect_simple_sink(write_binary_stream(
        null_output_stream(),
        std::make_shared<object_pool<std::vector<std::byte>>>(), 1));
}

namespace {

template <typename T, std::size_t N, std::size_t M>
auto equal_span(span<T const, N> lhs, span<T const, M> rhs) -> bool {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

struct mock_output_stream {
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MAKE_MOCK0(is_error, bool());
    MAKE_MOCK0(tell, std::optional<std::uint64_t>());
    MAKE_MOCK1(write, void(span<std::byte const>));
    // NOLINTEND(modernize-use-trailing-return-type)
};

template <typename OutputStream> class ref_output_stream {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    OutputStream &ref;

  public:
    explicit ref_output_stream(OutputStream &stream) : ref(stream) {}

    auto is_error() -> bool { return ref.is_error(); }
    auto tell() -> std::optional<std::uint64_t> { return ref.tell(); }
    void write(span<std::byte const> buf) { ref.write(buf); }
};

}; // namespace

TEST_CASE("write binary stream") {
    // We use a fixed writing granularity but vary event size and start offset
    // to test different cases.
    static constexpr auto granularity = 4;
    auto stream = mock_output_stream();
    auto proc = write_binary_stream<>(
        ref_output_stream(stream),
        std::make_shared<object_pool<std::vector<std::byte>>>(0, 1),
        granularity);

    using trompeloeil::_;

    SECTION("empty stream") {
        // Never interacts with output stream.
        proc.flush();
    }

    SECTION("zero size event") {
        ALLOW_CALL(stream, is_error()).RETURN(false);

        auto const start = GENERATE(0, 1, 4);
        ALLOW_CALL(stream, tell()).RETURN(start);
        proc.handle(own_on_copy_view<std::byte>());
        proc.handle(own_on_copy_view<std::byte>());
        proc.flush();
    }

    SECTION("initially bad stream") {
        ALLOW_CALL(stream, is_error()).RETURN(true);
        ALLOW_CALL(stream, tell()).RETURN(std::nullopt);
        proc.handle(own_on_copy_view<std::byte>()); // Empty spans are okay.
        std::array data{std::byte(0)};
        proc.handle(own_on_copy_view(data));
        proc.handle(own_on_copy_view(data));
        proc.handle(own_on_copy_view(data));
        REQUIRE_CALL(stream, write(_)).TIMES(1).WITH(_1.size() == granularity);
        REQUIRE_THROWS_WITH(proc.handle(own_on_copy_view(data)),
                            Catch::Matchers::ContainsSubstring("write"));
    }

    SECTION("tell() failure is ignored") {
        ALLOW_CALL(stream, is_error()).RETURN(false);
        REQUIRE_CALL(stream, tell()).TIMES(1, 4).RETURN(std::nullopt);
        std::array data{std::byte(0)};
        proc.handle(own_on_copy_view(data));
        proc.handle(own_on_copy_view(data));
        proc.handle(own_on_copy_view(data));
        REQUIRE_CALL(stream, write(_)).TIMES(1).WITH(_1.size() == granularity);
        proc.handle(own_on_copy_view(data));
        proc.flush();
    }

    SECTION("start offset 0") {
        auto const start = GENERATE(0, 4);
        ALLOW_CALL(stream, is_error()).RETURN(false);
        ALLOW_CALL(stream, tell()).RETURN(start);

        SECTION("event size 2") {
            std::array<std::uint8_t, 8> data{};
            std::iota(data.begin(), data.end(), std::uint8_t(0));
            auto const data_bytes = as_bytes(span(data));
            proc.handle(own_on_copy_view(data_bytes.subspan(0, 2)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(0, 4)));
            proc.handle(own_on_copy_view(data_bytes.subspan(2, 2)));
            proc.handle(own_on_copy_view(data_bytes.subspan(4, 2)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(4, 4)));

            SECTION("write fails") {
                ALLOW_CALL(stream, is_error()).RETURN(true);
                REQUIRE_THROWS(
                    proc.handle(own_on_copy_view(data_bytes.subspan(6, 2))));
            }

            SECTION("clean end") {
                proc.handle(own_on_copy_view(data_bytes.subspan(6, 2)));
                proc.flush();
            }
        }

        SECTION("event size 3") {
            std::array<std::uint8_t, 18> data{};
            std::iota(data.begin(), data.end(), std::uint8_t(0));
            auto const data_bytes = as_bytes(span(data));
            proc.handle(own_on_copy_view(data_bytes.subspan(0, 3)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(0, 4)));
            proc.handle(own_on_copy_view(data_bytes.subspan(3, 3)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(4, 4)));
            proc.handle(own_on_copy_view(data_bytes.subspan(6, 3)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(8, 4)));
            proc.handle(own_on_copy_view(data_bytes.subspan(9, 3)));
            proc.handle(own_on_copy_view(data_bytes.subspan(12, 3)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(12, 4)));

            SECTION("write fails") {
                ALLOW_CALL(stream, is_error()).RETURN(true);
                REQUIRE_THROWS(
                    proc.handle(own_on_copy_view(data_bytes.subspan(15, 3))));
            }

            SECTION("clean end") {
                proc.handle(own_on_copy_view(data_bytes.subspan(15, 3)));
                REQUIRE_CALL(stream, write(_))
                    .TIMES(1)
                    .WITH(equal_span(_1, data_bytes.subspan(16)));
                proc.flush();
            }
        }

        SECTION("event size 4") {
            std::array<std::uint8_t, 8> data{};
            std::iota(data.begin(), data.end(), std::uint8_t(0));
            auto const data_bytes = as_bytes(span(data));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(0, 4)));
            proc.handle(own_on_copy_view(data_bytes.subspan(0, 4)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(4, 4)));

            SECTION("write fails") {
                ALLOW_CALL(stream, is_error()).RETURN(true);
                REQUIRE_THROWS(
                    proc.handle(own_on_copy_view(data_bytes.subspan(4, 4))));
            }
        }

        SECTION("event size 5") {
            std::array<std::uint8_t, 15> data{};
            std::iota(data.begin(), data.end(), std::uint8_t(0));
            auto const data_bytes = as_bytes(span(data));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(0, 4)));
            proc.handle(own_on_copy_view(data_bytes.subspan(0, 5)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(4, 4)));
            proc.handle(own_on_copy_view(data_bytes.subspan(5, 5)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(8, 4)));
            ALLOW_CALL(stream, is_error()).RETURN(true);
            REQUIRE_THROWS(
                proc.handle(own_on_copy_view(data_bytes.subspan(10, 5))));
        }

        SECTION("event size 9") {
            std::array<std::uint8_t, 18> data{};
            std::iota(data.begin(), data.end(), std::uint8_t(0));
            auto const data_bytes = as_bytes(span(data));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(0, 8)));
            proc.handle(own_on_copy_view(data_bytes.subspan(0, 9)));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(8, 4)));

            SECTION("write fails") {
                ALLOW_CALL(stream, is_error()).RETURN(true);
                REQUIRE_THROWS(
                    proc.handle(own_on_copy_view(data_bytes.subspan(9, 9))));
            }

            SECTION("clean end") {
                REQUIRE_CALL(stream, write(_))
                    .TIMES(1)
                    .WITH(equal_span(_1, data_bytes.subspan(12, 4)));
                proc.handle(own_on_copy_view(data_bytes.subspan(9, 9)));
                REQUIRE_CALL(stream, write(_))
                    .TIMES(1)
                    .WITH(equal_span(_1, data_bytes.subspan(16, 2)));
                proc.flush();
            }
        }
    }

    SECTION("start offset 1") {
        auto const start = GENERATE(1, 5);
        ALLOW_CALL(stream, is_error()).RETURN(false);
        ALLOW_CALL(stream, tell()).RETURN(start);

        SECTION("event size 3") {
            std::array<std::uint8_t, 9> data{};
            std::iota(data.begin(), data.end(), std::uint8_t(0));
            auto const data_bytes = as_bytes(span(data));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(0, 3)));

            SECTION("write fails") {
                ALLOW_CALL(stream, is_error()).RETURN(true);
                REQUIRE_THROWS(
                    proc.handle(own_on_copy_view(data_bytes.subspan(0, 3))));
            }

            SECTION("clean end") {
                proc.handle(own_on_copy_view(data_bytes.subspan(0, 3)));
                proc.flush();
            }

            SECTION("continue") {
                proc.handle(own_on_copy_view(data_bytes.subspan(0, 3)));
                proc.handle(own_on_copy_view(data_bytes.subspan(3, 3)));
                REQUIRE_CALL(stream, write(_))
                    .TIMES(1)
                    .WITH(equal_span(_1, data_bytes.subspan(3, 4)));

                SECTION("write fails") {
                    ALLOW_CALL(stream, is_error()).RETURN(true);
                    REQUIRE_THROWS(proc.handle(
                        own_on_copy_view(data_bytes.subspan(6, 3))));
                }

                SECTION("clean end") {
                    proc.handle(own_on_copy_view(data_bytes.subspan(6, 3)));
                    REQUIRE_CALL(stream, write(_))
                        .TIMES(1)
                        .WITH(equal_span(_1, data_bytes.subspan(7, 2)));
                    proc.flush();
                }
            }
        }

        SECTION("event size 4") {
            std::array<std::uint8_t, 4> data{};
            std::iota(data.begin(), data.end(), std::uint8_t(0));
            auto const data_bytes = as_bytes(span(data));
            REQUIRE_CALL(stream, write(_))
                .TIMES(1)
                .WITH(equal_span(_1, data_bytes.subspan(0, 3)));

            SECTION("write fails") {
                ALLOW_CALL(stream, is_error()).RETURN(true);
                REQUIRE_THROWS(
                    proc.handle(own_on_copy_view(data_bytes.subspan(0, 4))));
            }

            SECTION("clean end") {
                proc.handle(own_on_copy_view(data_bytes.subspan(0, 4)));
                REQUIRE_CALL(stream, write(_))
                    .TIMES(1)
                    .WITH(equal_span(_1, data_bytes.subspan(3, 1)));
                proc.flush();
            }
        }
    }
}

TEST_CASE("write binary file with view as bytes") {
    auto stream = mock_output_stream();
    auto proc = view_as_bytes<int>(write_binary_stream<>(
        ref_output_stream(stream),
        std::make_shared<object_pool<std::vector<std::byte>>>(0, 1),
        2 * sizeof(int)));

    ALLOW_CALL(stream, is_error()).RETURN(false);
    ALLOW_CALL(stream, tell()).RETURN(0);
    using trompeloeil::_;

    std::vector const data{42, 43};
    auto const data_bytes = as_bytes(span(data));
    proc.handle(42);
    REQUIRE_CALL(stream, write(_)).TIMES(1).WITH(equal_span(_1, data_bytes));
    proc.handle(43);
    proc.flush();
}

} // namespace tcspc
