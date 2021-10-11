#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace flimevt {

namespace internal {

template <typename... Args> struct ApplyClassTemplateCurried {
    // Tmpl<T...> where T... is the first N element types of Tup, followed by
    // Ts.
    template <template <typename...> typename Tmpl, typename Tup,
              std::size_t N, typename... Ts>
    struct ApplyClassTemplateToTupleNElements {
      private:
        using Tn = std::remove_reference_t<decltype(std::get<N - 1>(
            std::declval<Tup>()))>;

      public:
        using type =
            typename ApplyClassTemplateToTupleNElements<Tmpl, Tup, N - 1, Tn,
                                                        Ts...>::type;
    };

    template <template <typename...> typename Tmpl, typename Tup,
              typename... Ts>
    struct ApplyClassTemplateToTupleNElements<Tmpl, Tup, 0, Ts...> {
        using type = Tmpl<Args..., Ts...>;
    };
};

} // namespace internal

// Metafunction; given Tup = std::tuple<T, U>, return Tmpl<Args..., T, U>.
template <template <typename...> typename Tmpl, typename Tup, typename... Args>
using ApplyClassTemplateToTupleElements =
    typename internal::ApplyClassTemplateCurried<Args...>::
        template ApplyClassTemplateToTupleNElements<Tmpl, Tup,
                                                    std::tuple_size_v<Tup>>;

template <template <typename...> typename Tmpl, typename Tup, typename... Args>
using ApplyClassTemplateToTupleElementsT =
    typename ApplyClassTemplateToTupleElements<Tmpl, Tup, Args...>::type;

} // namespace flimevt
