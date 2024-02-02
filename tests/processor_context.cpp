/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/processor_context.hpp"

#include <catch2/catch_all.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace tcspc {

TEST_CASE("processor context and tracker") {
    auto ctx = std::make_shared<processor_context>();

    struct test_accessor {
        void *tracker_addr;
    };

    CHECK_THROWS(ctx->accessor<test_accessor>("nonexistent"));

    // Scope to test tracker destruction.
    {
        auto trk = ctx->tracker<test_accessor>("myproc");
        trk.register_accessor_factory(
            [](auto &tracker) { return test_accessor{&tracker}; });
        CHECK(ctx->accessor<test_accessor>("myproc").tracker_addr == &trk);

        // No duplicates.
        CHECK_THROWS(ctx->tracker<test_accessor>("myproc"));

        auto moved_trk = std::move(trk);
        CHECK(ctx->accessor<test_accessor>("myproc").tracker_addr ==
              &moved_trk);

        processor_tracker<test_accessor> move_assigned_trk;
        move_assigned_trk = std::move(moved_trk);
        CHECK(ctx->accessor<test_accessor>("myproc").tracker_addr ==
              &move_assigned_trk);
    }

    CHECK_THROWS(ctx->accessor<test_accessor>("myproc"));

    // No duplicates even after destruction.
    CHECK_THROWS(ctx->tracker<test_accessor>("myproc"));
}

namespace {

struct example_access {
    // An accessor shoulud be a single (unparameterized) type per processor
    // template. Type erasure of the processor can be afforded by storing
    // std::function instances for actual access to the processor.
    std::function<int()> value;
};

// Workaround for spurious clang-tidy warning:
// NOLINTNEXTLINE(bugprone-exception-escape)
class example_processor {
    // Processor data.
    int value = 42;

    // Downstream omitted from example.

    // Cold data after downstream. The tracker should be here, since it is
    // accessed at much lower frequency than the actual processing.
    processor_tracker<example_access> trk;

  public:
    // Processors supporting context-base access should have a constructor that
    // takes a tracker as its first parameter (and also an otherwise equivalent
    // constructor that does not; not shown).
    explicit example_processor(processor_tracker<example_access> &&tracker)
        : trk(std::move(tracker)) {
        trk.register_accessor_factory(
            // We register a callable that can create an accessor given the
            // tracker. The accessor is only valid while the processor (and
            // therefore its tracker) stay alive in its current location, so
            // within the callable we can make these assumptions.
            [](processor_tracker<example_access> &t) {
                auto *self =
                    LIBTCSPC_PROCESSOR_FROM_TRACKER(example_processor, trk, t);
                // Finally, we use lambda(s) to supply the type-erased
                // function(s) by which the accessor interacts with the
                // processor. As stated above, we assume 'self' remains valid
                // during the lifetime of the accessor.
                return example_access{[self]() -> int { return self->value; }};
            });
    }

    // Event handlers and flush() omitted from example.
};

} // namespace

TEST_CASE("processor tracker intended use") {
    auto ctx = std::make_shared<processor_context>();
    {
        // The context is injected into a processor upon creation, when later
        // access to the processor is desired.
        auto proc =
            example_processor(ctx->tracker<example_access>("test_proc"));

        // Then, the processor can be accessed by name at a later time, even if
        // the processor has been moved.
        CHECK(ctx->accessor<example_access>("test_proc").value() == 42);
        auto moved_proc = std::move(proc);
        CHECK(ctx->accessor<example_access>("test_proc").value() == 42);
    }
    CHECK_THROWS(ctx->accessor<example_access>("test_proc"));
}

} // namespace tcspc
