/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc_py/handle_span.hpp"

#include "libtcspc/span.hpp"

#include <Python.h>

namespace {

struct fake_processor {
    void handle(tcspc::span<unsigned short const> /* s */) {}
    void handle(int const & /* i */) {}
    void flush() {}
};

// NOLINTBEGIN(misc-include-cleaner)
[[maybe_unused]] auto instantiate_template(fake_processor &proc,
                                           PyObject *pyobj) {
    return tcspc::py::handle_buffer(proc, pyobj);
}
// NOLINTEND(misc-include-cleaner)

} // namespace

auto main() -> int {
    // Actual unit tests are written in Python. The main point of this program
    // is to catch compile errors early and to provide a usable configuration
    // for clang-tidy to use.
    return 0;
}
