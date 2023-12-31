/*
 * This file is part of libtcspc
 * Copyright 2019-2023 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "libtcspc/apply_class_template.hpp"

namespace tcspc::internal {

namespace {

template <typename... Ts> struct some_class_tmpl {};

} // namespace

static_assert(
    std::is_same_v<apply_class_template_t<some_class_tmpl, std::tuple<>,
                                          unsigned, double>,
                   some_class_tmpl<unsigned, double>>);

static_assert(std::is_same_v<
              apply_class_template_t<some_class_tmpl, std::tuple<int, float>>,
              some_class_tmpl<int, float>>);

static_assert(std::is_same_v<
              apply_class_template_t<some_class_tmpl, std::tuple<int, float>,
                                     unsigned, double>,
              some_class_tmpl<unsigned, double, int, float>>);

} // namespace tcspc::internal
