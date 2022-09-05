/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#include "FLIMEvents/ApplyClassTemplateToTupleElements.hpp"

using namespace flimevt;

template <typename... Ts> struct Test {};

static_assert(std::is_same_v<ApplyClassTemplateToTupleElementsT<
                                 Test, std::tuple<>, unsigned, double>,
                             Test<unsigned, double>>);

static_assert(std::is_same_v<
              ApplyClassTemplateToTupleElementsT<Test, std::tuple<int, float>>,
              Test<int, float>>);

static_assert(
    std::is_same_v<ApplyClassTemplateToTupleElementsT<
                       Test, std::tuple<int, float>, unsigned, double>,
                   Test<unsigned, double, int, float>>);
