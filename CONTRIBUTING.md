<!--
This file is part of libtcspc
Copyright 2019-2024 Board of Regents of the University of Wisconsin System
SPDX-License-Identifier: MIT
-->

# Contributing

At the moment this document describes the technical aspects (coding
standards/guidelines). Pull requests are welcome.

## General coding guidelines

The code is C++17.

Code should pass the pre-commit hook (including formatting by clang-format).

Code should have no build warnings (on supported platform-compiler
combinations) and no warnings from clang-tidy.

Suppressing clang-tidy warnings (with `NOLINT` comments) is allowed if
well-considered. The reason for suppression should be left as a comment if not
obvious from the context.

For things that are not mechanically detected or corrected, follow the existing
convention (for example, names are in `snake_case` except for template
parameters, which are in `PascalCase`).

All new code should be accompanied with good unit tests. API functions and
types should be documented with Doxygen comments (follow existing practice).

## Guidelines for event types

- Event types should be named with the suffix `_event`.
- Events should be simple data objects with no behavior.
  - Often, they can (and should) be a `struct` with public data members.
  - No user-defined constructor is needed; brace initialization can be used.
  - Exception: raw vendor record events should be `memcpy`-able from the raw
    data.
    - Use a single public data member, `std::array<std::byte, N> bytes`.
    - Provide accessors for each conceptual field (often a bit range).
- All events must be movable and copyable, and default-constructible.
  - Usually there should be no need for user-defined move/copy constructors and
    assignment operators (Rule of Zero).
  - If a data member is a large array that is expensive to copy, it can be an
    `own_on_copy_view`.
    - The producer of the event does not copy the data when creating the
      `own_on_copy_view`.
    - Well-designed downstream processors will not copy events unless
      necessary, so will not suffer any overhead.
    - When making a copy of the event _is_ necessary, it works seamlessly.
      - This often comes up in testing (the `capture_output` processor) where
        we don't care about the cost of copying.
      - It is up to the user to avoid copying into a buffering processor if
        that is not what is desired.
- Events should define `operator==` and `operator!=` as hidden friend functions
  (friend functions defined inside the class definition and participating only
  in argument-dependent lookup).
- Events should define the stream insertion `operator<<` as a hidden friend
  function.
- Events should define `swap(a, b)` as a hidden friend function if `std::swap`
  would be inefficient or unavailable for the type.
- A member `swap(other)` function is unnecessary and should be avoided to
  reduce clutter.

## Guidelines for processor types

- Processors (or, rather, the factory functions that create them; see below on
  this) should be named after a verb describing what the processor does.
  - For example, `count`, not `counter`.
  - Exception: special wrapper processors such as `type_erased_processor`.
- Processors are class templates, whose last template parameter is the
  downstream processor type (`typename Downstream` by convention).
  - Sinks, of course, will not have a downstream.
- Processors can be non-copyable if necessary. Non-movable processors are
  strongly discouraged.
- Ordinary processors contain the (chain of) downstream processor(s) in a data
  member (`Downstream downstream`).
  - This should usually be the last non-static data member, so that the data
    layout mirrors the order of processing. Cold data (such as data only
    accessed when terminating processing) should be placed after the downstream
    member.
  - Note that the processor becomes move-only if any of its downstream
    processors are move-only.
  - Exception: processors with special semantics such as `merge` or
    `type_erased_processor`.
  - If the user wants to break the chain of containment for any reason, they
    can use `type_erased_processor`.
    - This can be of advantage in a few cases:
      - To allow runtime selection of the processor type (with
        `type_erased_processor`).
      - Possibly to reduce compile time.
- Constructor
  - Processors should not have a default constructor.
    - Exception: sinks with no configuration parameters.
  - Processors should have an `explicit` constructor (or a few) taking
    configuration parameters and, as the last parameter, the downstream
    processor (`Downstream downstream`).
    - The downstream processor should usually be assigned to the data member
      via `std::move()`.
- Factory function
  - Processors in the library are usually defined as class templates in an
    internal namespace.
  - A free function template should be provided for constructing the processor,
    given configuration parameters and the downstream processor. This function
    returns the new processor by value.
  - This generally works better than CTAD and is easier than writing deduction
    guides. (CTAD only works if all template parameters can be deduced; we
    often want to deduce just the downstream type and possibly _some_ of the
    compile-time configuration parameters.)
  - The downstream processor should be the last parameter
    (`Downstream &&downstream` and passed to the constructor via
    `std::forward`)
- Copy and move constructors, assignment operators, and destructor
  - For regular (data-processing) processors, follow the Rule of Zero.
    - This should usually result in a copyable processor, unless the downstream
      is move-only.
  - For processors with special semantics, follow the Rule of Five to make the
    processor move-only (or, very rarely, non-movable).
    - Even then, it should be extremely rare that a non-defaulted destructor is
      needed.
- Avoid const data members
  - Even if a configuration parameter will be stored and never modified, it is
    simpler to leave it non-const so that copy and move assignment work.
- Avoid reference data members
  - These also prevent assignment. If you think a reference data member is
    necessary due to interfacing constraints, consider using a pointer instead.
- Event handlers
  - `void handle(MyEvent const &event)` (never `noexcept`)
  - The parameter should almost always be passed by `const &` unless it will be
    stored (moved).
  - For specific events, use overloads; for generic events (e.g., for
    processors that pass through unrelated events), use a member function
    template:
    `template <typename AnyEvent> void handle(AnyEvent const &event)`.
  - Event handlers may throw (see below).
- The `flush()` function
  - `void flush()` (never `noexcept`)
  - This function is called when the stream of events ends without an error.
    Implementations should flush any beffered events and call the downstream's
    `flush()`.
  - `flush()` may throw (see below).
- Calling downstream event handlers
  - Downstream event handlers may be called within event handlers and
    `flush()`. They may not be called during processor construction or
    destruction.
    - So if there is an event that must be sent at the beginning of the stream,
      it must be sent lazily upon receiving the first event (or the end of an
      empty stream). This seems to be rarely needed.
    - Exception: Buffering processors may emit events from an asynchronous
      context (but still not in the constructor or destructor). This can be
      seen as the buffering processor acting as a sink to one stream and a
      source to another, rather than a processor within a single stream.
  - When calling the downstream processor's event handler, if the event to be
    passed will later be reused (and therefore should have its value
    preserved), always wrap in `std::as_const()`.
    - Usually this is simply a safety check in case the downstream processor
      forgot to define the event handler parameter as by-value or `const &`.
    - If the sent event (or its value) will _not_ ever be used again,
      `std::as_const()` should not be applied.
    - In theory, we could get better performance in a few cases by modifying
      events in place. This would require all processors to implement rvalue
      ref versions of `handle()` in addition to the const lvalue ref version.
      We do not currently take advantage of this, as it is not clear that it
      will make a significant difference.
- Ending the event stream
  - If the event stream ends because the source reached an end, the source
    calls `flush()` on the first processor; this is propagated down the chain
    to flush all processors in order.
  - If a processor, while handling an event or a flush, determines that
    processing should stop (because the end of the data of interest was
    detected), it should call `flush()` on the downstream and throw
    `end_processing`.
  - If a processor, while handling an event or a flush, encounters an error, it
    should (without flushing the downstream) throw an exception derived from
    `std::exception`.
  - `flush()` must not be called in processor constructors and destructors,
    only event handlers and `flush()`.
  - Each processor's `flush()` may be called at most once during its lifetime,
    and never after one of its event handlers has thrown.
  - Event handlers may not be called on a thrown or flushed processor.
  - The `merge` processor provides special behavior, allowing both upstreams to
    be flushed independently.
  - Because `flush()` is only called and propagated on successful end of the
    stream, user code must arrange to notify the ultimate sink(s) when the
    stream is halted by an error, if necessary.
- Equality comparison and stream insertion operators are unnecessary for
  processors.
- An overload of the `swap` function is generally unnecessary for processors.
