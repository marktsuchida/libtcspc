/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc_py/handle_span.hpp"

#include "libtcspc/span.hpp"

#include <Python.h>

struct fake_processor {
    void handle([[maybe_unused]] tcspc::span<unsigned short const> s) {}
    void handle([[maybe_unused]] int const &i) {}
    void flush() {}
};

// NOLINTNEXTLINE(misc-include-cleaner)
auto instantiate_template(fake_processor &proc, PyObject *pyobj) {
    return tcspc::py::handle_buffer(proc, pyobj);
}

auto main() -> int {
    // Actual unit tests are written in Python. The main point of this program
    // is to catch compile errors early and to provide a usable configuration
    // for clang-tidy to use.
    return 0;
}
