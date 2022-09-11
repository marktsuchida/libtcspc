/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/ApplyClassTemplateToTupleElements.hpp"

using namespace flimevt::internal;

template <typename... Ts> struct Test {};

static_assert(std::is_same_v<apply_class_template_to_tuple_elements_t<
                                 Test, std::tuple<>, unsigned, double>,
                             Test<unsigned, double>>);

static_assert(
    std::is_same_v<
        apply_class_template_to_tuple_elements_t<Test, std::tuple<int, float>>,
        Test<int, float>>);

static_assert(
    std::is_same_v<apply_class_template_to_tuple_elements_t<
                       Test, std::tuple<int, float>, unsigned, double>,
                   Test<unsigned, double, int, float>>);
