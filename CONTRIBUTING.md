<!--
This file is part of FLIMEvents
Copyright 2019-2022 Board of Regents of the University of Wisconsin System
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
    `autocopy_span`.
    - The producer of the event does not copy the data when creating the
      `autocopy_span`.
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
  - For example, `count_event`, not `event_counter`.
  - Exception: special wrapper processors such as `ref_processor`,
    `type_erased_processor`
- Processors are class templates, whose last template parameter is the
  downstream processor type (`typename D` by convention).
  - Sinks, of course, will not have a downstream.
- Processors can by non-copyable and even non-movable, but only if necessary.
  - Non-movable processors are strongly discouraged ourside of special use
    cases (e.g., `capture_output`). They typically require `ref_processor` to
    use, unless they are the most upstream processor (i.e., data source).
- Ordinary processors contain the (chain of) downstream processor(s) in a data
  member (`D downstream`).
  - This should usually be the last non-static data member, so that the data
    layout mirrors the order of processing.
  - Note that the processor becomes move-only if any of its downstream
    processors are move-only.
  - Exception: processors with special semantics such as `ref_processor` and
    `type_erased_processor`.
  - If the user wants to break the chain of containment for any reason, they
    can use `ref_processor` or `type_erased_processor`.
    - This can be of advantage in a few cases:
      - To retain access to the downstream processor after construction of the
        upstream (with `ref_processor`).
      - To allow runtime selection of the processor type (with
        `type_erased_processor`).
      - To reduce compile time.
      - Note: Currently even the use of these reference-semantic processors
        does not allow upstream processors to be created before their
        downstream. There is no plan to change this, as processor chaings are
        designed to be single-use (i.e., used only for one event stream and
        then discarded).
  - The `merge` processor also implicitly breaks the chain of containment
    (because we can't contain one downstream in two separate upstreams!).
- Constructor
  - Processors should not have a default constructor.
    - Exception: `discard_any`, `discard_all`; sinks with no configuration
      parameters.
  - Processors should have an `explicit` constructor (or a few) taking
    configuration parameters and, as the last parameter, the downstream
    processor (as a forwarding reference: `D &&downstream`).
    - The downstream processor should be assigned to the data member via
      `std::forward<D>()`.
- Factory function
  - Processors in the library are defined as class templates in an internal
    namespace.
  - A free function template should be provided for constructing the processor,
    given configuration parameters and the downstream processor. This function
    returns the new processor by value.
  - This generally works better than CTAD and is easier than writing deduction
    guides. (CTAD only works if all template parameters can be deduced; we
    often want to deduce just the downstream type and possibly _some_ of the
    compile-time configuration parameters.)
- Copy and move constructors, assignment operators, and destructor
  - For regular (data-processing) processors, follow the Rule of Zero.
    - This should usually result in a copyable processor, unless the downstream
      is move-only.
  - For processors with special semantics, follow the Rule of Five to make the
    processor move-only (or, rarely, non-movable).
    - Even then, it should be extremely rare that a non-defaulted destructor is
      needed.
- Avoid const data members
  - Even if a configuration parameter will be stored and never modified, it is
    simpler to leave it non-const so that copy and move assignment work.
- Avoid reference data members
  - These also prevent assignment. If you think a reference data member is
    necessary due to interfacing constraints, consider using a pointer instead.
- The `handle_end()` function
  - `void handle_end(std::exception_ptr const &error) noexcept`
  - The parameter should almost always be passed by `const &` unless it will be
    stored (moved) (copying and destroying `exception_ptr` may be expensive).
- Event handlers
  - `void handle_event(MyEvent const &event) noexcept`
  - The parameter should almost always be passed by `const &` unless it will be
    stored (moved).
  - For specific events, use overloads; for generic events (e.g., for
    processors that pass through unrelated events), use a member function
    template:
    `template <typename E> void handle_event(E const &event) noexcept`.
  - Event handlers must not throw. If there is an error, they should call
    `handle_end()` on the downstream (and arange to ignore subsequent events).
- Calling downstream event handlers
  - Downstream event handlers may be called within event handlers and
    `handle_end`. They may not be called during processor construction or
    destruction.
    - So if there is an event that must be sent at the beginning of the stream,
      it must be sent lazily upon receiving the first event (or the end of an
      empty stream). This seems to be rarely needed.
    - Exception: Buffering processors may emit events from an asynchronous
      context (but still not in the constructor or destructor).
  - When calling the downstream processor's event handler, and the event to be
    passed will later be reused (and therefore should have its value
    preserved), always wrap in `std::as_const()`.
    - Usually this is simply a safety check in case the downstream processor
      forgot to define the event handler parameter as by-value or `const &`.
    - If the sent event (or its value) will _not_ ever be used again,
      `std::as_const()` should not be applied.
    - In theory, we could get better performance in a few cases by modifying
      events in place. This would require all processors to implement rvalue
      ref versions of `handle_event` in addition to the const lvalue ref
      version. We do not currently take advantage of this, as it is not clear
      that it will make a significant difference.
- Ending the event stream
  - The downstream processor's `handle_end` may be called within event handlers
    and `handle_end`. It may not be called during processor construction or
    destruction.
    - Exception: Buffering processors may emit end-of-stream from an
      asynchronous context (but still not in the constructor or destructor).
  - Calls to the downstream processor's event handlers can be in any order.
    However, a call to the downstream's `handle_end` must occur at most once,
    after which no event handlers may be called.
    - Note that this means processors need to discard received events from
      upstream if it has stopped processing on its own accord because it had an
      error or because it detected the end of data of interest.
    - Processors need not check for unexpected events following the end of
      stream.
  - `handle_end` must call the downstream's `handle_end` if it has not yet been
    called.
    - The application usually relies on the end-of-stream being propagated in
      this manner to know when processing has finished (or failed) and the
      processor chain can be destroyed (and also the data source may be
      stopped).
  - Processors must be safe to destroy without having called `handle_end`.
    - This applies whether or not any events were received.
- Equality comparison and stream insertion operators are unnecessary for
  processors.
- An overload of the `swap` function is generally unnecessary for processors.
