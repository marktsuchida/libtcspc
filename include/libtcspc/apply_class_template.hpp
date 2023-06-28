/*
 * This file is part of FLIMEvents
 * Copyright 2019-2022 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace tcspc::internal {

namespace really_internal {

template <typename... Args> struct apply_class_template_curried {
    // Tmpl<T...> where T... is the first N element types of Tup, followed by
    // Ts.
    template <template <typename...> typename Tmpl, typename Tup,
              std::size_t N, typename... Ts>
    struct apply_class_template_to_tuple_n_elements {
      private:
        using Tn = std::remove_reference_t<decltype(std::get<N - 1>(
            std::declval<Tup>()))>;

      public:
        using type =
            typename apply_class_template_to_tuple_n_elements<Tmpl, Tup, N - 1,
                                                              Tn, Ts...>::type;
    };

    template <template <typename...> typename Tmpl, typename Tup,
              typename... Ts>
    struct apply_class_template_to_tuple_n_elements<Tmpl, Tup, 0, Ts...> {
        using type = Tmpl<Args..., Ts...>;
    };
};

} // namespace really_internal

// Metafunction; given Tup = std::tuple<T, U>, return Tmpl<Args..., T, U>.
template <template <typename...> typename Tmpl, typename Tup, typename... Args>
using apply_class_template =
    typename really_internal::apply_class_template_curried<Args...>::
        template apply_class_template_to_tuple_n_elements<
            Tmpl, Tup, std::tuple_size_v<Tup>>;

template <template <typename...> typename Tmpl, typename Tup, typename... Args>
using apply_class_template_t =
    typename apply_class_template<Tmpl, Tup, Args...>::type;

} // namespace tcspc::internal
