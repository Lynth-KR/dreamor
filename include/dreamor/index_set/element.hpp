// SPDX-License-Identifier: Apache-2.0
// File: element.hpp
// What each makeSet argument contributes to the element tuple, and the element tuple built
// from all of them.
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/index_set/column.hpp"

namespace dreamor::internal {

// The three ways an AMPL index set names a column:
//
//     m in MACHINES        a container       -> one column, the same range every time
//     i in ITEMS[m]     a function        -> one column, the pointers that apply to this
//                                            binding (a reference to a stored list avoids
//                                            an allocation per binding)
//     (m,i) in ASSIGN    an existing set   -> as many columns as that set has
enum class SourceKind : std::uint8_t { kContainer, kDependent, kSpread };

template <typename T>
struct IsTuple : std::false_type {};
template <typename... Ts>
struct IsTuple<std::tuple<Ts...>> : std::true_type {};

// A range of tuples is always read as a spread, so a single column whose value is itself a
// tuple cannot be expressed - use a struct for that value.
template <typename Source>
[[nodiscard]] consteval SourceKind sourceKindOf() {
  if constexpr (std::ranges::forward_range<Source&>) {
    return IsTuple<std::ranges::range_value_t<Source>>::value ? SourceKind::kSpread : SourceKind::kContainer;
  } else {
    return SourceKind::kDependent;
  }
}

template <typename A, typename B>
struct Concat;
template <typename... As, typename... Bs>
struct Concat<std::tuple<As...>, std::tuple<Bs...>> {
  using type = std::tuple<As..., Bs...>;
};

// The empty case lands on the primary template, whose assertion depends on Cols and so
// fires only when a source actually runs out of column types to consume.
template <typename Cols>
struct SplitFront {
  static_assert(std::tuple_size_v<Cols> > 0,
                "Not enough column types: give one per source that introduces a new "
                "column. Sources that spread an existing set bring their own.");
  // Dummies, so the assertion above is the only diagnostic.
  using head = std::tuple<>;
  using tail = std::tuple<>;
};
template <typename Head, typename... Tail>
struct SplitFront<std::tuple<Head, Tail...>> {
  using head = std::tuple<Head>;
  using tail = std::tuple<Tail...>;
};

// The element tuple, built by walking the sources in order: a spread source contributes the
// columns of the set it spreads, every other source consumes one explicit type.
template <bool IsSpread, typename Acc, typename Cols, typename Source, typename... Rest>
struct BuildElemStep;

template <typename Acc, typename Cols, typename... Sources>
struct BuildElem;

template <typename Acc, typename Cols>
struct BuildElem<Acc, Cols> {
  static_assert(std::tuple_size_v<Cols> == 0,
                "More column types than columns to name. Sources that spread an existing "
                "set bring their own column types.");
  using type = Acc;
};

template <typename Acc, typename Cols, typename Source, typename... Rest>
struct BuildElem<Acc, Cols, Source, Rest...>
    : BuildElemStep<sourceKindOf<Source>() == SourceKind::kSpread, Acc, Cols, Source, Rest...> {};

template <typename Acc, typename Cols, typename Source, typename... Rest>
struct BuildElemStep<true, Acc, Cols, Source, Rest...>
    : BuildElem<typename Concat<Acc, std::ranges::range_value_t<Source>>::type, Cols, Rest...> {};

template <typename Acc, typename Cols, typename Source, typename... Rest>
struct BuildElemStep<false, Acc, Cols, Source, Rest...>
    : BuildElem<typename Concat<Acc, typename SplitFront<Cols>::head>::type, typename SplitFront<Cols>::tail, Rest...> {
};

// Ranges as a view over the caller's container, functions by value.
template <typename Source>
[[nodiscard]] auto holdSource(Source&& source) {
  using Bare = std::remove_reference_t<Source>;
  if constexpr (std::ranges::forward_range<Bare&>) {
    return std::views::all(source);
  } else {
    return std::forward<Source>(source);
  }
}

// A dependent source names which entities apply; it does not make them. So it yields
// pointers: a range of entities is a range of copies, and their addresses are not the ones
// the rest of the model uses. Views are exempt - they own nothing.
template <typename Range, typename Column>
[[nodiscard]] consteval bool dependentRangeYieldsPointers() {
  using Bare = std::remove_cvref_t<Range>;
  if constexpr (!is_column<Column> || !std::is_pointer_v<typename Column::Value>) {
    return true;  // A value column copies the value out.
  } else {
    return std::is_pointer_v<std::ranges::range_value_t<Bare>> || std::ranges::view<Bare>;
  }
}

// What a callable returns when applied to its own columns; `invocable` guards the case
// where invoke_result_t would be a hard error.
template <typename Callable, typename Cols>
struct ReturnTypeOf {
  using type = void;

  static constexpr bool invocable = false;
};
template <typename Callable, typename... Cols>
  requires std::invocable<Callable, Cols...>
struct ReturnTypeOf<Callable, std::tuple<Cols...>> {
  using type = std::invoke_result_t<Callable, Cols...>;

  static constexpr bool invocable = true;
};

// How many element-tuple columns a source contributes.
template <typename Source>
[[nodiscard]] consteval std::size_t columnCountOf() {
  if constexpr (sourceKindOf<Source>() == SourceKind::kSpread) {
    return std::tuple_size_v<std::ranges::range_value_t<Source>>;
  } else {
    return 1;
  }
}

}  // namespace dreamor::internal
