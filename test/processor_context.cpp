/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/processor_context.hpp"

#include <catch2/catch_all.hpp>

#include <cstddef>

namespace tcspc {

TEST_CASE("processor context and tracker", "[processor_context]") {
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
    // In this example the processor is not a template, but most real
    // processors have very long type names due to the downstream being
    // included. To implement an accessor, it is best to use an alias type.
    using self_type = example_processor;

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
                // Note: offsetof() on non-standard-layout types is only
                // "conditionally-supported" as of C++17. I expect this not to
                // be a problem in practice, but it should be noted.
                static constexpr std::size_t tracker_offset =
                    offsetof(self_type, trk);
                // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
                auto *self = reinterpret_cast<self_type *>(
                    reinterpret_cast<std::byte *>(&t) - tracker_offset);
                // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
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

TEST_CASE("processor tracker intended use", "[processor_context]") {
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
