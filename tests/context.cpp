/*
 * This file is part of libtcspc
 * Copyright 2019-2026 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <utility>

namespace tcspc {

TEST_CASE("context and tracker") {
    auto ctx = context::create();

    struct test_accessor {
        void *tracker_addr;
    };

    CHECK_THROWS(ctx->access<test_accessor>("nonexistent"));

    // Scope to test tracker destruction.
    {
        auto trk = ctx->tracker<test_accessor>("myproc");
        trk.register_accessor_factory(
            [](auto &tracker) { return test_accessor{&tracker}; });
        CHECK(ctx->access<test_accessor>("myproc").tracker_addr == &trk);

        // No duplicates.
        CHECK_THROWS(ctx->tracker<test_accessor>("myproc"));

        auto moved_trk = std::move(trk);
        CHECK(ctx->access<test_accessor>("myproc").tracker_addr == &moved_trk);

        access_tracker<test_accessor> move_assigned_trk;
        move_assigned_trk = std::move(moved_trk);
        CHECK(ctx->access<test_accessor>("myproc").tracker_addr ==
              &move_assigned_trk);
    }

    CHECK_THROWS(ctx->access<test_accessor>("myproc"));

    // No duplicates even after destruction.
    CHECK_THROWS(ctx->tracker<test_accessor>("myproc"));
}

namespace {

struct example_accessor {
    // An accessor should be a single (unparameterized) type per object type or
    // template. Type erasure of the object can be afforded by storing
    // std::function instances for actual access to the object.
    std::function<int()> value;
};

class example_processor {
    // Processor data.
    int value = 42;

    // Downstream omitted from example.

    // Cold data after downstream. The tracker should be here, since it is
    // accessed at much lower frequency than the actual processing.
    access_tracker<example_accessor> trk;

  public:
    // Processors supporting context-base access should have a constructor that
    // takes a tracker as its first parameter.
    explicit example_processor(access_tracker<example_accessor> &&tracker)
        : trk(std::move(tracker)) {
        trk.register_accessor_factory(
            // We register a callable that can create an accessor given the
            // tracker. The accessor is only valid while the processor (and
            // therefore its tracker) stay alive in its current location, so
            // within the callable we can make these assumptions.
            [](access_tracker<example_accessor> &t) {
                auto *self =
                    LIBTCSPC_OBJECT_FROM_TRACKER(example_processor, trk, t);
                // Finally, we use lambda(s) to supply the type-erased
                // function(s) by which the accessor interacts with the
                // processor. As stated above, we assume 'self' remains valid
                // during the lifetime of the accessor.
                return example_accessor{
                    [self]() -> int { return self->value; }};
            });
    }

    // Event handlers and flush() omitted from example.
};

} // namespace

TEST_CASE("access tracker intended use") {
    auto ctx = context::create();
    {
        // The context is injected into the processor upon creation.
        auto proc =
            example_processor(ctx->tracker<example_accessor>("test_proc"));

        // Then, the processor can be accessed by name at a later time, even if
        // the processor has been moved.
        CHECK(ctx->access<example_accessor>("test_proc").value() == 42);
        auto moved_proc = std::move(proc);
        CHECK(ctx->access<example_accessor>("test_proc").value() == 42);
    }
    CHECK_THROWS(ctx->access<example_accessor>("test_proc"));
}

} // namespace tcspc
