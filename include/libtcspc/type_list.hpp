/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common.hpp"

#include <cstddef>
#include <type_traits>

namespace tcspc {

/**
 * \brief Compile-time representation of a list of types.
 *
 * \ingroup type-list
 *
 * In libtcspc, specializations of \c type_list are frequently used to specify
 * sets of events to be processed in a certain way.
 *
 * We use a "list" of types, rather than a "set", because there is no way to
 * implement a type that has set semantics (because there is no total order of
 * types available at compile time). Therefore, the order of the types \p Ts is
 * significant for the purpose of type identity (\c std::is_same). However, we
 * provide metafunctions to perform set operations on \c type_list
 * specializations.
 */
template <typename... Ts> struct type_list {
    type_list() = delete;
};

/**
 * \defgroup is-type-list Metafunction is_type_list
 * \ingroup type-list
 * \copydoc is_type_list
 * @{
 */

/**
 * \brief Metafunction to tell if a type is a \c type_list type.
 *
 * Checks if \p T is a specialization of \c type_list and provides the result
 * in the \c bool member \c value.
 *
 * \see is_type_list_v
 */
template <typename T> struct is_type_list : std::false_type {};

/** \cond implementation-detail */

template <typename... Ts>
struct is_type_list<type_list<Ts...>> : std::true_type {};

/** \endcond */

/**
 * \brief Helper variable template for \c is_type_list.
 */
template <typename T> constexpr bool is_type_list_v = is_type_list<T>::value;

/** @} <!-- group is-type-list --> */

/**
 * \defgroup type-list-size Metafunction type_list_size
 * \ingroup type-list
 * \copydoc type_list_size
 * @{
 */

/**
 * \brief Metafunction to obtain the size (length) of a type list.
 *
 * Provides the size of the \c type_list specialization \p TypeList in the \c
 * std::size_t member \c value.
 *
 * No deduplication is performed on the type arguments of \p TypeList.
 *
 * \see type_list_size_v
 */
template <typename TypeList> struct type_list_size;

/** \cond implementation-detail */

template <typename... Ts>
struct type_list_size<type_list<Ts...>>
    : std::integral_constant<std::size_t, sizeof...(Ts)> {};

/** \endcond */

/**
 * \brief Helper variable template for \c type_list_size.
 */
template <typename TypeList>
constexpr std::size_t type_list_size_v = type_list_size<TypeList>::value;

/** @} <!-- group type-list-size --> */

/**
 * \defgroup type-list-singleelem Metafunction type_list_singleton_element
 * \ingroup type-list
 * \copydoc type_list_singleton_element
 * @{
 */

/**
 * \brief Metafunction to obtain the contained type of a singleton type list.
 *
 * Extracts the first type argument of the \c type_list specialization \p
 * TypeList, which must have a size of 1, and provides it as the member \c
 * type.
 *
 * \see type_list_singleton_element_t
 */
template <typename TypeList> struct type_list_singleton_element;

/** \cond implementation-detail */

template <typename T>
struct type_list_singleton_element<type_list<T>> : internal::type_identity<T> {
};

/** \endcond */

/**
 * \brief Helper type for \c type_list_singleton_element.
 */
template <typename TypeList>
using type_list_singleton_element_t =
    typename type_list_singleton_element<TypeList>::type;

/** @} <!-- group type-list-singleelem --> */

/**
 * \defgroup type-list-contains Metafunction type_list_contains
 * \ingroup type-list
 * \copydoc type_list_contains
 * @{
 */

/**
 * \brief Metafunction to determine if a type is contained in a type list.
 *
 * Checks if \p Type is in the template arguments of the \c type_list
 * specialization \p TypeList and provides the result in the \c bool member \c
 * value.
 *
 * \see type_list_contains_v
 */
template <typename TypeList, typename Type> struct type_list_contains;

/** \cond implementation-detail */

template <typename Type, typename... Ts>
struct type_list_contains<type_list<Ts...>, Type>
    : std::disjunction<std::is_same<Type, Ts>...> {};

/** \endcond */

/**
 * \brief Helper variable template for \c type_list_contains.
 */
template <typename TypeList, typename Type>
constexpr bool type_list_contains_v =
    type_list_contains<TypeList, Type>::value;

/** @} <!-- group type-list-contains --> */

/**
 * \defgroup type-list-is-subset Metafunction type_list_is_subset
 * \ingroup type-list
 * \copydoc type_list_is_subset
 * @{
 */

/**
 * \brief Metafunction to determine if a type list is a subset of another.
 *
 * Determines if all type arguments of the \c type_list specialization \p TL0
 * are contained by the \c type_list specialization \p TL1 and provides the
 * result in the \c bool member \c value.
 *
 * \see type_list_is_subset_v
 */
template <typename TL0, typename TL1> struct type_list_is_subset;

/** \cond implementation-detail */

template <typename... T0s, typename TL1>
struct type_list_is_subset<type_list<T0s...>, TL1>
    : std::conjunction<type_list_contains<TL1, T0s>...> {};

/** \endcond */

/**
 * \brief Helper variable template for \c type_list_is_subset.
 */
template <typename TL0, typename TL1>
constexpr bool type_list_is_subset_v = type_list_is_subset<TL0, TL1>::value;

/** @} <!-- group type-list-is-subset --> */

/**
 * \defgroup type-list-is-equal-set Metafunction type_list_is_equal_set
 * \ingroup type-list
 * \copydoc type_list_is_equal_set
 * @{
 */

/**
 * \brief Metafunction to determine if a type list is set-equivalent to
 * another.
 *
 * Determines if the type arguments of the \c type_list specializations \p TL0
 * and \p TL1 match, disregarding ordering and duplication, and provides the
 * result in the \c bool member \c value.
 *
 * \see type_list_is_equal_set_v
 */
template <typename TL0, typename TL1>
struct type_list_is_equal_set
    : std::conjunction<type_list_is_subset<TL0, TL1>,
                       type_list_is_subset<TL1, TL0>> {};

/**
 * \brief Helper variable template for \c type_list_is_equal_set.
 */
template <typename TL0, typename TL1>
constexpr bool type_list_is_equal_set_v =
    type_list_is_equal_set<TL0, TL1>::value;

/** @} <!-- group type-list-is-equal-set --> */

/**
 * \defgroup unique-type-list Metafunction unique_type_list
 * \ingroup type-list
 * \copydoc unique_type_list
 * @{
 */

namespace internal {

template <typename Dest, typename Src> struct unique_type_list_impl;

template <typename... Ds>
struct unique_type_list_impl<type_list<Ds...>, type_list<>>
    : type_identity<type_list<Ds...>> {};

template <typename... Ds, typename T, typename... Ts>
struct unique_type_list_impl<type_list<Ds...>, type_list<T, Ts...>>
    : std::conditional_t<
          type_list_contains_v<type_list<Ds...>, T>,
          unique_type_list_impl<type_list<Ds...>, type_list<Ts...>>,
          unique_type_list_impl<type_list<Ds..., T>, type_list<Ts...>>> {};

} // namespace internal

/**
 * \brief Metafunction to remove duplicate types from a type list.
 *
 * Removes duplicate types from the \c type_list specialization \p TypeList and
 * provides the result as member \c type.
 *
 * \see unique_type_list_t
 */
template <typename TypeList>
struct unique_type_list
    : internal::unique_type_list_impl<type_list<>, TypeList> {};

/**
 * \brief Helper type for \c unique_type_list.
 */
template <typename TypeList>
using unique_type_list_t = typename unique_type_list<TypeList>::type;

/** @} <!-- group unique-type-list --> */

/**
 * \defgroup type-list-union Metafunction type_list_union
 * \ingroup type-list
 * \copydoc type_list_union
 * @{
 */

namespace internal {

template <typename Dest, typename Src0, typename Src1>
struct type_list_union_impl;

template <typename... Ds>
struct type_list_union_impl<type_list<Ds...>, type_list<>, type_list<>>
    : type_identity<type_list<Ds...>> {};

template <typename... Ds, typename S1, typename... S1s>
struct type_list_union_impl<type_list<Ds...>, type_list<>,
                            type_list<S1, S1s...>>
    : std::conditional_t<
          type_list_contains_v<type_list<Ds...>, S1>,
          type_list_union_impl<type_list<Ds...>, type_list<>,
                               type_list<S1s...>>,
          type_list_union_impl<type_list<Ds..., S1>, type_list<>,
                               type_list<S1s...>>> {};

template <typename... Ds, typename S0, typename... S0s, typename... S1s>
struct type_list_union_impl<type_list<Ds...>, type_list<S0, S0s...>,
                            type_list<S1s...>>
    : std::conditional_t<
          type_list_contains_v<type_list<Ds...>, S0>,
          type_list_union_impl<type_list<Ds...>, type_list<S0s...>,
                               type_list<S1s...>>,
          type_list_union_impl<type_list<Ds..., S0>, type_list<S0s...>,
                               type_list<S1s...>>> {};

} // namespace internal

/**
 * \brief Metafunction to obtain the union of two sets of types.
 *
 * Forms a \c type_list specialization containing the set-union of the types
 * contained in the \c type_list specializations \p TL0 and \p TL1 and provides
 * the result as member \c type.
 *
 * In the resulting type, the type arguments are ordered by first appearance in
 * the concatenation of \p TL0 and \p TL1.
 *
 * \see type_list_union_t
 */
template <typename TL0, typename TL1>
struct type_list_union
    : internal::type_list_union_impl<type_list<>, TL0, TL1> {};

/**
 * \brief Helper type for \c type_list_union.
 */
template <typename TL0, typename TL1>
using type_list_union_t = typename type_list_union<TL0, TL1>::type;

/** @} <!-- group type-list-union --> */

/**
 * \defgroup type-list-intersection Metafunction type_list_intersection
 * \ingroup type-list
 * \copydoc type_list_intersection
 * @{
 */

namespace internal {

template <typename Dest, typename Src0, typename Src1>
struct type_list_intersection_impl;

template <typename... Ds, typename... S1s>
struct type_list_intersection_impl<type_list<Ds...>, type_list<>,
                                   type_list<S1s...>>
    : type_identity<type_list<Ds...>> {};

template <typename... Ds, typename S0, typename... S0s, typename... S1s>
struct type_list_intersection_impl<type_list<Ds...>, type_list<S0, S0s...>,
                                   type_list<S1s...>>
    : std::conditional_t<
          type_list_contains_v<type_list<S1s...>, S0>,
          type_list_intersection_impl<type_list<Ds..., S0>, type_list<S0s...>,
                                      type_list<S1s...>>,
          type_list_intersection_impl<type_list<Ds...>, type_list<S0s...>,
                                      type_list<S1s...>>> {};

} // namespace internal

/**
 * \brief Metafunction to obtain the intersection of two sets of types.
 *
 * Forms a \c type_list specialization containing the set-intersection of the
 * types contained in the \c type_list specializations \p TL0 and \p TL1 and
 * provides the result as member \c type.
 *
 * In the resulting type, the type arguments are ordered by appearance in \p
 * TL0. If \p TL0 contains duplicate type arguments (not recommended usage),
 * the resulting type may also contain duplicate type arguments.
 *
 * \see type_list_intersection_t
 */
template <typename TL0, typename TL1>
struct type_list_intersection
    : internal::type_list_intersection_impl<type_list<>, TL0, TL1> {};

/**
 * \brief Helper type for \c type_list_intersection.
 */
template <typename TL0, typename TL1>
using type_list_intersection_t =
    typename type_list_intersection<TL0, TL1>::type;

/** @} <!-- group type-list-intersection --> */

/**
 * \defgroup type-list-set-difference Metafunction type_list_set_difference
 * \ingroup type-list
 * \copydoc type_list_set_difference
 * @{
 */

namespace internal {

template <typename Dest, typename Src0, typename Src1>
struct type_list_set_difference_impl;

template <typename... Ds, typename... S1s>
struct type_list_set_difference_impl<type_list<Ds...>, type_list<>,
                                     type_list<S1s...>>
    : type_identity<type_list<Ds...>> {};

template <typename... Ds, typename S0, typename... S0s, typename... S1s>
struct type_list_set_difference_impl<type_list<Ds...>, type_list<S0, S0s...>,
                                     type_list<S1s...>>
    : std::conditional_t<
          type_list_contains_v<type_list<S1s...>, S0>,
          type_list_set_difference_impl<type_list<Ds...>, type_list<S0s...>,
                                        type_list<S1s...>>,
          type_list_set_difference_impl<
              type_list<Ds..., S0>, type_list<S0s...>, type_list<S1s...>>> {};

} // namespace internal

/**
 * \brief Metafunction to obtain the set difference of two sets of types.
 *
 * Forms a \c type_list specialization containing the set difference of the
 * types contained in the \c type_list specializations \p TL0 and \p TL1
 * (namely, the types contained by \p TL0 but not by \p TL1) and provides the
 * result as member \c type.
 *
 * In the resulting type, the type arguments are ordered by appearance in \p
 * TL0. If \p TL0 contains duplicate type arguments (not recommended usage),
 * the resulting type may also contain duplicate type arguments.
 *
 * \see type_list_set_difference_t
 */
template <typename TL0, typename TL1>
struct type_list_set_difference
    : internal::type_list_set_difference_impl<type_list<>, TL0, TL1> {};

/**
 * \brief Helper type for \c type_list_set_difference.
 */
template <typename TL0, typename TL1>
using type_list_set_difference_t =
    typename type_list_set_difference<TL0, TL1>::type;

} // namespace tcspc
