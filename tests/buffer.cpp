/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/buffer.hpp"

#include "libtcspc/arg_wrappers.hpp"
#include "libtcspc/common.hpp"
#include "libtcspc/context.hpp"
#include "libtcspc/errors.hpp"
#include "test_checkers.hpp"
#include "test_thread_utils.hpp"

// Trompeloeil requires catch2 to be included first, but does not define which
// subset of Catch2 3.x is required. So include catch_all.hpp.
#include <catch2/catch_all.hpp>
#include <catch2/trompeloeil.hpp>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace tcspc {

TEST_CASE("introspect buffer", "[introspect]") {
    auto ctx = context::create();
    check_introspect_simple_processor(
        buffer<int>(arg::threshold<std::size_t>{1},
                    ctx->tracker<buffer_access>("buf"), null_sink()));
    check_introspect_simple_processor(real_time_buffer<int>(
        arg::threshold<std::size_t>{1}, std::chrono::seconds(1),
        ctx->tracker<buffer_access>("rtbuf"), null_sink()));
}

// We use Trompeloeil rather than feed_input/capture_output for fine-grain
// testing of buffers.

namespace {

struct mock_downstream {
    MAKE_MOCK1(handle, void(int));
    MAKE_MOCK0(flush, void());
};

// Reference-semantic processor, to work with Trompeloeil mocks that cannot be
// moved.
template <typename Downstream> class ref_proc {
    Downstream *d;

  public:
    explicit ref_proc(Downstream &downstream) : d(&downstream) {}

    template <typename Event> void handle(Event const &event) {
        d->handle(event);
    }

    void flush() { d->flush(); }
};

} // namespace

TEST_CASE("buffer moves out rvalue events") {
    auto ctx = context::create();
    auto buf = buffer<std::unique_ptr<int>>(arg::threshold<std::size_t>{3},
                                            ctx->tracker<buffer_access>("buf"),
                                            null_sink());
    auto event = std::make_unique<int>(42);
    buf.handle(std::move(event));
    CHECK_FALSE(event);
}

TEST_CASE("buffer empty stream") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = buffer<int>(arg::threshold<std::size_t>{3},
                           ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            buf_acc.pump();
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        REQUIRE_CALL(out, flush()).TIMES(1);
        buf.flush();
        t.join();
    }

    SECTION("pump only after flush") {
        buf.flush();
        REQUIRE_CALL(out, flush()).TIMES(1);
        buf_acc.pump();
    }
}

TEST_CASE("buffer stream ended downstream") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = buffer<int>(arg::threshold<std::size_t>{1},
                           ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        bool threw = false;
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            try {
                buf_acc.pump();
            } catch (end_of_processing const &) {
                threw = true;
            }
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        REQUIRE_CALL(out, handle(42)).TIMES(1).THROW(end_of_processing({}));
        buf.handle(42);
        t.join();
        CHECK(threw);
    }

    SECTION("pump only after input") {
        buf.handle(42);
        REQUIRE_CALL(out, handle(42)).TIMES(1).THROW(end_of_processing({}));
        CHECK_THROWS_AS(buf_acc.pump(), end_of_processing);
    }
}

TEST_CASE("buffer stream error downstream") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = buffer<int>(arg::threshold<std::size_t>{1},
                           ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        bool threw = false;
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            try {
                buf_acc.pump();
            } catch (std::runtime_error const &) {
                threw = true;
            }
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        REQUIRE_CALL(out, handle(42)).TIMES(1).THROW(std::runtime_error(""));
        buf.handle(42);
        t.join();
        CHECK(threw);
    }

    SECTION("pump only after input") {
        buf.handle(42);
        REQUIRE_CALL(out, handle(42)).TIMES(1).THROW(std::runtime_error(""));
        CHECK_THROWS_AS(buf_acc.pump(), std::runtime_error);
    }
}

TEST_CASE("buffer stream halted") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = buffer<int>(arg::threshold<std::size_t>{1},
                           ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        bool threw = false;
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            try {
                buf_acc.pump();
            } catch (source_halted const &) {
                threw = true;
            }
        });
        thread_start_latch.wait();
        // Wait for the pump loop to start (best-effort).
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // No calls allowed to mock 'out'.
        buf_acc.halt();
        t.join();
        CHECK(threw);
    }

    SECTION("pump only after halt") {
        buf_acc.halt();
        CHECK_THROWS_AS(buf_acc.pump(), source_halted);
    }
}

TEST_CASE("buffer does not emit events before threshold") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = buffer<int>(arg::threshold<std::size_t>{3},
                           ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        bool threw = false;
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            try {
                buf_acc.pump();
            } catch (source_halted const &) {
                threw = true;
            }
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        buf.handle(42);
        buf.handle(43);
        wait_a_little(); // Likely catch any incorrect call to out.
        buf_acc.halt();
        t.join();
        CHECK(threw);
    }

    SECTION("pump only after input") {
        buf.handle(42);
        buf.handle(43);
        buf_acc.halt();
        CHECK_THROWS_AS(buf_acc.pump(), source_halted);
    }
}

TEST_CASE("buffer flushes stored events") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = buffer<int>(arg::threshold<std::size_t>{3},
                           ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            buf_acc.pump();
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        buf.handle(42);
        buf.handle(43);
        wait_a_little(); // Likely catch any incorrect call to out.
        REQUIRE_CALL(out, handle(42)).TIMES(1);
        REQUIRE_CALL(out, handle(43)).TIMES(1);
        REQUIRE_CALL(out, flush()).TIMES(1);
        buf.flush();
        t.join();
    }

    SECTION("pump only after flush") {
        buf.handle(42);
        buf.handle(43);
        buf.flush();
        REQUIRE_CALL(out, handle(42)).TIMES(1);
        REQUIRE_CALL(out, handle(43)).TIMES(1);
        REQUIRE_CALL(out, flush()).TIMES(1);
        buf_acc.pump();
    }
}

TEST_CASE("buffer emits stored events on reaching threshold") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = buffer<int>(arg::threshold<std::size_t>{3},
                           ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            buf_acc.pump();
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        buf.handle(42);
        buf.handle(43);
        wait_a_little(); // Likely catch any incorrect call to out.
        {
            latch event44_emit_latch(1);
            REQUIRE_CALL(out, handle(42)).TIMES(1);
            REQUIRE_CALL(out, handle(43)).TIMES(1);
            REQUIRE_CALL(out, handle(44))
                .TIMES(1)
                .LR_SIDE_EFFECT(event44_emit_latch.count_down());
            buf.handle(44);
            event44_emit_latch.wait();
        }
        REQUIRE_CALL(out, flush()).TIMES(1);
        buf.flush();
        t.join();
    }

    SECTION("pump only after input") {
        buf.handle(42);
        buf.handle(43);
        buf.handle(44);
        latch pump_start_latch(1);
        std::thread t([&] {
            pump_start_latch.wait();
            buf_acc.pump();
        });
        {
            latch event44_emit_latch(1);
            REQUIRE_CALL(out, handle(42)).TIMES(1);
            REQUIRE_CALL(out, handle(43)).TIMES(1);
            REQUIRE_CALL(out, handle(44))
                .TIMES(1)
                .LR_SIDE_EFFECT(event44_emit_latch.count_down());
            pump_start_latch.count_down();
            event44_emit_latch.wait();
        }
        REQUIRE_CALL(out, flush()).TIMES(1);
        buf.flush();
        t.join();
    }
}

TEMPLATE_TEST_CASE("buffer input throws after downstream stopped", "",
                   end_of_processing, std::runtime_error) {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = buffer<int>(arg::threshold<std::size_t>{1},
                           ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        bool threw = false;
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            try {
                buf_acc.pump();
            } catch (TestType const &) {
                threw = true;
            }
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        {
            REQUIRE_CALL(out, handle(42)).TIMES(1).THROW(TestType(""));
            buf.handle(42);
            t.join();
            CHECK(threw);
        }

        SECTION("next event") {
            CHECK_THROWS_AS(buf.handle(43), end_of_processing);
        }

        SECTION("flush") { CHECK_THROWS_AS(buf.flush(), end_of_processing); }
    }
}

TEST_CASE(
    "real_time_buffer with large latency does not emit before threshold") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto buf = real_time_buffer<int>(
        arg::threshold<std::size_t>{3}, std::chrono::hours(1),
        ctx->tracker<buffer_access>("buf"), ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        bool threw = false;
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            try {
                buf_acc.pump();
            } catch (source_halted const &) {
                threw = true;
            }
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        buf.handle(42);
        buf.handle(43);
        wait_a_little(); // Likely catch any incorrect call to out.
        buf_acc.halt();
        t.join();
        CHECK(threw);
    }

    SECTION("pump only after input") {
        buf.handle(42);
        buf.handle(43);
        buf_acc.halt();
        CHECK_THROWS_AS(buf_acc.pump(), source_halted);
    }
}

TEST_CASE("real_time_buffer emits event before threshold after latency") {
    auto ctx = context::create();
    auto out = mock_downstream();
    auto const latency = std::chrono::microseconds(100);
    auto buf = real_time_buffer<int>(arg::threshold<std::size_t>{2}, latency,
                                     ctx->tracker<buffer_access>("buf"),
                                     ref_proc(out));
    auto buf_acc = ctx->access<buffer_access>("buf");

    SECTION("pump in thread") {
        latch thread_start_latch(1);
        std::thread t([&] {
            thread_start_latch.count_down();
            buf_acc.pump();
        });
        thread_start_latch.wait();
        wait_a_little(); // For likely pump loop start.
        {
            latch event42_emit_latch(1);
            REQUIRE_CALL(out, handle(42))
                .TIMES(1)
                .LR_SIDE_EFFECT(event42_emit_latch.count_down());
            auto const start_time = std::chrono::steady_clock::now();
            buf.handle(42);
            event42_emit_latch.wait();
            auto const stop_time = std::chrono::steady_clock::now();
            CHECK(stop_time - start_time >= latency);
        }
        REQUIRE_CALL(out, flush()).TIMES(1);
        buf.flush();
        t.join();
    }
}

} // namespace tcspc
