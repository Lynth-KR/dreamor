// SPDX-License-Identifier: Apache-2.0
// File: binding.hpp
// Which columns a callable is bound to. A predicate or a dependent source names its columns
// in its own parameter list; this reads them off &Pred::operator() and resolves each to a
// position in the element tuple.
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>  // IWYU pragma: keep // For std::function detection
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/index_set/column.hpp"

namespace dreamor::internal {

template <typename T>
struct is_std_function : std::false_type {};
template <typename R, typename... Args>
struct is_std_function<std::function<R(Args...)>> : std::true_type {};

// A function pointer or std::function is an indirect call per candidate tuple, and cannot
// be inlined.
template <typename Callable>
concept no_indirect_call = !std::is_pointer_v<Callable> && !std::is_function_v<std::remove_reference_t<Callable>> &&
                           !is_std_function<std::remove_cvref_t<Callable>>::value;

// --- Locating a callable's columns ------------------------------------------------------

// The parameter list of a non-overloaded, non-template operator() const.
template <typename Pred>
struct PredicateSignature {
  static constexpr bool deducible = false;
};
template <typename R, typename Class, typename... Params>
struct PredicateSignature<R (Class::*)(Params...) const> {
  static constexpr bool deducible = true;

  using type = std::tuple<std::remove_cvref_t<Params>...>;
};

template <typename Pred>
concept has_deducible_signature = requires { &std::remove_cvref_t<Pred>::operator(); } &&
                                  PredicateSignature<decltype(&std::remove_cvref_t<Pred>::operator())>::deducible;

// Explicit declaration first, then the signature. A callable with a template operator() -
// Membership, say - has no signature to read, so it declares its columns instead.
template <typename Pred>
struct ColumnsOfPredicate {
  static constexpr bool known = false;
  // Keeps a failed `known` assert the only diagnostic. It also zeroes the placement mask,
  // and a zero mask never fires - so `known` must stay a hard error at every entry point,
  // or an unreadable predicate would be dropped instead of rejected.
  using type = std::tuple<>;
};
template <typename Pred>
  requires requires { typename PredicateColumns<std::remove_cvref_t<Pred>>::type; }
struct ColumnsOfPredicate<Pred> {
  static constexpr bool known = true;

  using type = typename PredicateColumns<std::remove_cvref_t<Pred>>::type;
};
template <typename Pred>
  requires(!requires { typename PredicateColumns<std::remove_cvref_t<Pred>>::type; } && has_deducible_signature<Pred>)
struct ColumnsOfPredicate<Pred> {
  static constexpr bool known = true;

  using type = typename PredicateSignature<decltype(&std::remove_cvref_t<Pred>::operator())>::type;
};

// Position of a column in the element tuple, or tuple_size when absent. makeSet guarantees
// the column types are distinct, so a type identifies exactly one position.
template <typename Column, typename Tup, std::size_t I = 0>
[[nodiscard]] consteval std::size_t indexOfColumn() {
  if constexpr (I >= std::tuple_size_v<Tup>) {
    return std::tuple_size_v<Tup>;
  } else if constexpr (std::is_same_v<Column, std::tuple_element_t<I, Tup>>) {
    return I;
  } else {
    return indexOfColumn<Column, Tup, I + 1>();
  }
}

template <typename Columns, typename ElemTuple>
struct PredicatePlacement;
template <typename... Columns, typename ElemTuple>
struct PredicatePlacement<std::tuple<Columns...>, ElemTuple> {
  static constexpr bool all_named = (is_column<Columns> && ...);
  // Vacuously true when the columns are not named, so all_named is the only diagnostic.
  static constexpr bool all_present =
      !all_named || ((indexOfColumn<Columns, ElemTuple>() < std::tuple_size_v<ElemTuple>) && ...);
  // The columns it reads, as a bitmask over the element tuple. Zero unless every column
  // resolved, so a malformed signature cannot look like a dependency on a column that does
  // not exist.
  static constexpr std::uint64_t mask =
      (all_named && all_present) ? ((std::uint64_t{1} << indexOfColumn<Columns, ElemTuple>()) | ... | std::uint64_t{0})
                                 : 0;
  using indices = std::index_sequence<indexOfColumn<Columns, ElemTuple>()...>;
};

template <typename Pred, typename ElemTuple>
using PlacementOf = PredicatePlacement<typename ColumnsOfPredicate<Pred>::type, ElemTuple>;

// Placed the same way a predicate is, so ITEMS[m] is `[](MachineCol m)` wherever the machine
// sits. Its columns are also its ordering constraint: the source cannot be visited until
// every one of them is bound.
template <typename Source, typename ElemTuple>
using SourcePlacement = PlacementOf<Source, ElemTuple>;

}  // namespace dreamor::internal
