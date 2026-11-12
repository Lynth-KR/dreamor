// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The columns that a column callable reads. A condition or a lateral source lists its columns in its parameter list;
// this header reads the list from &Pred::operator() and finds the position of each column in the row.
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/internal/index_set/column.hpp"

/// @section Column callables
///
/// The rules of this section say which callables dreamor accepts where it calls a callable with the columns of a row.
/// Such a callable is a column callable: a lambda or a function object whose parameters are columns. dreamor reads
/// the parameter list to find the columns that the callable reads. dreamor then calls the callable with those columns
/// of each row, wherever the columns are in the row. A condition, a term of a sum, a bound of a `VarTable`, a key of
/// `orderBy` and a lateral source are column callables.
///
/// A column callable meets these requirements:
///
/// - The callable has exactly one `operator()`, which is `const` and is not a template.
/// - The callable has at least one parameter.
/// - Each parameter is a column of the row, and no column appears twice.
/// - Each parameter is taken by value or by `const` reference.
///
/// A function pointer, a `std::function`, a generic lambda and a `mutable` lambda do not compile, and neither does any
/// callable whose `operator()` is a template: a callable states the columns that it reads in its signature.
///
/// Guide: guide/columns.md

namespace dreamor::internal::inline index_set {

template <typename T>
struct is_std_function : std::false_type {};

template <typename R, typename... Args>
struct is_std_function<std::function<R(Args...)>> : std::true_type {};

// A function pointer or a std::function makes an indirect call for every row, which the compiler cannot inline.
template <typename Callable>
concept no_indirect_call = !std::is_pointer_v<Callable> && !std::is_function_v<std::remove_reference_t<Callable>> &&
                           !is_std_function<std::remove_cvref_t<Callable>>::value;

// Reads the parameter list of an operator() const that is neither overloaded nor a template. A noexcept operator()
// needs a specialization of its own, because C++17 made noexcept part of the function type.
template <typename Pred>
struct PredicateSignature {
  static constexpr bool deducible = false;
};

template <typename R, typename Class, typename... Params>
struct PredicateSignature<R (Class::*)(Params...) const> {
  static constexpr bool deducible = true;
  // Each parameter must take its column by value or by const reference. A row is read-only, so a non-const lvalue
  // reference or an rvalue reference to a column cannot bind to it.
  static constexpr bool no_mutable_reference =
      ((!std::is_reference_v<Params> ||
        (std::is_lvalue_reference_v<Params> && std::is_const_v<std::remove_reference_t<Params>>)) &&
       ...);

  using type = std::tuple<std::remove_cvref_t<Params>...>;
};

template <typename R, typename Class, typename... Params>
struct PredicateSignature<R (Class::*)(Params...) const noexcept> : PredicateSignature<R (Class::*)(Params...) const> {
};

template <typename Pred>
concept deducible_signature = requires { &std::remove_cvref_t<Pred>::operator(); } &&
                              PredicateSignature<decltype(&std::remove_cvref_t<Pred>::operator())>::deducible;

// A specialization of CallableColumns takes precedence over the parameter list. A callable with a template
// operator(), such as Membership, has no parameter list to read, so it declares its columns in a specialization.
template <typename Pred>
struct ColumnsOfPredicate {
  static constexpr bool known                = false;
  static constexpr bool no_mutable_reference = true;
  // The empty tuple keeps the failed static_assert on `known` the only error. It also makes the placement mask zero,
  // and a callable with a zero mask is never called. Every entry point must therefore keep `known` a hard error;
  // otherwise a condition whose parameters cannot be read would be dropped instead of rejected.
  using type = std::tuple<>;
};

template <typename Pred>
  requires requires { typename CallableColumns<std::remove_cvref_t<Pred>>::type; }
struct ColumnsOfPredicate<Pred> {
  static constexpr bool known                = true;
  static constexpr bool no_mutable_reference = true;

  using type = CallableColumns<std::remove_cvref_t<Pred>>::type;
};

template <typename Pred>
  requires(!requires { typename CallableColumns<std::remove_cvref_t<Pred>>::type; } && deducible_signature<Pred>)
struct ColumnsOfPredicate<Pred> {
  static constexpr bool known = true;
  static constexpr bool no_mutable_reference =
      PredicateSignature<decltype(&std::remove_cvref_t<Pred>::operator())>::no_mutable_reference;

  using type = PredicateSignature<decltype(&std::remove_cvref_t<Pred>::operator())>::type;
};

// Returns the position of Column in the row, or tuple_size if the row has no such column. A chain keeps the column
// types distinct, so a type identifies exactly one position.
template <typename Column, typename Tup>
[[nodiscard]] consteval std::size_t indexOfColumn() {
  return kIndexIn<Column, typename RowPack<Tup>::type>;
}

template <typename Columns, typename ElemTuple>
struct PredicatePlacement;

template <typename... Columns, typename ElemTuple>
struct PredicatePlacement<std::tuple<Columns...>, ElemTuple> {
  static constexpr bool all_named = (column<Columns> && ...);
  // True if not every parameter is a column, so that the check of all_named reports the only error.
  static constexpr bool all_present =
      !all_named || ((indexOfColumn<Columns, ElemTuple>() < std::tuple_size_v<ElemTuple>) && ...);
  // If a callable reads a column twice, both parameters get the same value. That is almost always a mistake for
  // another column, because a callable needs each column once. True if not every parameter is a column, for the
  // same reason as all_present.
  static constexpr bool no_repeated_column =
      !all_named || allColumnsDistinct<std::tuple<Columns...>>(std::index_sequence_for<Columns...>{});
  // The columns that the callable reads, as a bitmask over the positions of the row. The mask is zero unless every
  // column was found, so that a wrong parameter list cannot look like a dependency on a column that does not exist.
  static constexpr std::uint64_t mask =
      (all_named && all_present) ? ((std::uint64_t{1} << indexOfColumn<Columns, ElemTuple>()) | ... | std::uint64_t{0})
                                 : 0;
  using indices = std::index_sequence<indexOfColumn<Columns, ElemTuple>()...>;
};

// The message for a parameter that is a non-const reference, shared by every place that calls a callable with the
// columns of a row.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_READ_ONLY_COLUMNS_MESSAGE                                                              \
  "Take each column by value - `[](cMachine m)` - or by const reference: a row is read-only, so a `cMachine&` " \
  "cannot bind to it."

// The message for a callable whose parameter list cannot be read, shared by every place that reads one. The list is
// read from the type of operator(). If the body does not compile, an auto return type cannot be deduced and the type
// is unknown, as it is for a template operator(). C++ cannot tell the two cases apart, so the message names both.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_READ_PARAMETERS_MESSAGE                                                                 \
  "Cannot read this callable's parameter list: its body does not compile - the error above - or its operator() " \
  "is a template, `[](auto, auto)`, which does not say which columns it reads. Fix the body, or give it one "    \
  "concrete signature, its columns as its parameters: [](cMachine m, cPeriod t)."

// The message for a parameter that is not a column, shared by every place that calls a callable with the columns of
// a row.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_TAKE_COLUMNS_MESSAGE                                                                   \
  "Take the columns themselves - `[](cMachine m, cItem i)` - not their values, a Ref or an entity: a callable " \
  "over a set is called with its rows' columns."

template <typename Pred, typename ElemTuple>
using PlacementOf = PredicatePlacement<typename ColumnsOfPredicate<Pred>::type, ElemTuple>;

// A lateral source is placed as a condition is, by the columns that it reads, wherever they are in the row. These
// columns also constrain the visit order: the source is visited only after every one of them is bound.
template <typename Source, typename ElemTuple>
using SourcePlacement = PlacementOf<Source, ElemTuple>;

}  // namespace dreamor::internal::inline index_set
