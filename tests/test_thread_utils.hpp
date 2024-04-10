/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <thread>

// C++20 latch equivalent
class latch {
    mutable std::mutex mut;
    mutable std::condition_variable cv;
    std::ptrdiff_t ct;

  public:
    [[maybe_unused]] static constexpr std::ptrdiff_t max =
        std::numeric_limits<std::ptrdiff_t>::max();

    explicit latch(std::ptrdiff_t count) : ct(count) {}
    ~latch() = default;
    latch(latch const &) = delete;
    auto operator=(latch const &) = delete;
    latch(latch &&) = delete;
    auto operator=(latch &&) = delete;

    void count_down(std::ptrdiff_t n = 1) {
        bool should_notify{};
        {
            std::scoped_lock const lock(mut);
            ct -= n;
            should_notify = ct == 0;
        }
        if (should_notify)
            cv.notify_all();
    }

    auto try_wait() const noexcept -> bool {
        std::scoped_lock const lock(mut);
        return ct == 0;
    }

    void wait() const {
        std::unique_lock lock(mut);
        cv.wait(lock, [&] { return ct == 0; });
    }

    void arrive_and_wait(std::ptrdiff_t n = 1) {
        bool should_notify{};
        {
            std::unique_lock lock(mut);
            ct -= n;
            should_notify = ct == 0;
            if (not should_notify)
                return cv.wait(lock, [&] { return ct == 0; });
        }
        if (should_notify)
            cv.notify_all();
    }
};

inline void wait_a_little() noexcept {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
