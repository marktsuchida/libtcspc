/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/ApplyClassTemplateToTupleElements.hpp"

using namespace flimevt::internal;

template <typename... Ts> struct some_class_tmpl {};

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
