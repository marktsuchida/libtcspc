/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/processor_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace tcspc {

TEST_CASE("context and tracker") {
    auto ctx = context::create();

    struct test_access {
        void *tracker_addr;
    };

    CHECK_THROWS(ctx->access<test_access>("nonexistent"));

    // Scope to test tracker destruction.
    {
        auto trk = ctx->tracker<test_access>("myproc");
        trk.register_access_factory(
            [](auto &tracker) { return test_access{&tracker}; });
        CHECK(ctx->access<test_access>("myproc").tracker_addr == &trk);

        // No duplicates.
        CHECK_THROWS(ctx->tracker<test_access>("myproc"));

        auto moved_trk = std::move(trk);
        CHECK(ctx->access<test_access>("myproc").tracker_addr == &moved_trk);

        access_tracker<test_access> move_assigned_trk;
        move_assigned_trk = std::move(moved_trk);
        CHECK(ctx->access<test_access>("myproc").tracker_addr ==
              &move_assigned_trk);
    }

    CHECK_THROWS(ctx->access<test_access>("myproc"));

    // No duplicates even after destruction.
    CHECK_THROWS(ctx->tracker<test_access>("myproc"));
}

namespace {

struct example_access {
    // An access shoulud be a single (unparameterized) type per object type or
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
    access_tracker<example_access> trk;

  public:
    // Processors supporting context-base access should have a constructor that
    // takes a tracker as its first parameter.
    explicit example_processor(access_tracker<example_access> &&tracker)
        : trk(std::move(tracker)) {
        trk.register_access_factory(
            // We register a callable that can create an access given the
            // tracker. The access is only valid while the processor (and
            // therefore its tracker) stay alive in its current location, so
            // within the callable we can make these assumptions.
            [](access_tracker<example_access> &t) {
                auto *self =
                    LIBTCSPC_OBJECT_FROM_TRACKER(example_processor, trk, t);
                // Finally, we use lambda(s) to supply the type-erased
                // function(s) by which the access interacts with the
                // processor. As stated above, we assume 'self' remains valid
                // during the lifetime of the access.
                return example_access{[self]() -> int { return self->value; }};
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
            example_processor(ctx->tracker<example_access>("test_proc"));

        // Then, the processor can be accessed by name at a later time, even if
        // the processor has been moved.
        CHECK(ctx->access<example_access>("test_proc").value() == 42);
        auto moved_proc = std::move(proc);
        CHECK(ctx->access<example_access>("test_proc").value() == 42);
    }
    CHECK_THROWS(ctx->access<example_access>("test_proc"));
}

} // namespace tcspc
