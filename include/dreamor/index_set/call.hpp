// SPDX-License-Identifier: Apache-2.0
// File: call.hpp
// Calling a callable with the columns it names, out of one row of an index set.
//
// Shared by everything that takes column callables over materialized rows: sumOf's term and
// predicates, VarTable's per-row bounds. makeSet's predicates and sources go through the
// generation plan instead (plan.hpp), since they run on partial tuples.
#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/index_set/binding.hpp"
#include "dreamor/index_set/column.hpp"
#include "dreamor/index_set/element.hpp"

namespace dreamor::internal {

// A row is one element of an index set: a tuple of named columns.
template <typename Row>
[[nodiscard]] consteval bool isIndexSetRow() {
  if constexpr (IsTuple<Row>::value) {
    return allElementsAreColumns<Row>(std::make_index_sequence<std::tuple_size_v<Row>>{});
  } else {
    return false;
  }
}

// Calls a callable with just the columns it named. The row is already materialized and a
// column is identified by its type, so std::get finds each one - no index bookkeeping.
template <typename Cols>
struct CallColumnsOf;
template <typename... Cols>
struct CallColumnsOf<std::tuple<Cols...>> {
  template <typename Callable, typename Row>
  [[nodiscard]] static decltype(auto) call(const Callable& callable, const Row& row) {
    return callable(std::get<Cols>(row)...);
  }
};

// A callable that takes a whole row as well as its columns, and is faster given the row: a
// variable table finds a row of its own set by the row's address. The backends specialize
// it; anything else is called with its columns, as it declared them.
template <typename Callable>
struct TakesWholeRow : std::false_type {};

// Guarded on the row, so a range that is not an index set produces the one diagnostic
// its caller (sumOf, VarTable) already reports instead of one per call site.
template <typename Callable, typename Row>
[[nodiscard]] decltype(auto) callColumnsOf(const Callable& callable, const Row& row) {
  if constexpr (TakesWholeRow<Callable>::value &&
                std::is_same_v<typename ColumnsOfPredicate<Callable>::type, std::remove_cvref_t<Row>>) {
    return callable(row);
  } else if constexpr (isIndexSetRow<std::remove_cvref_t<Row>>()) {
    return CallColumnsOf<typename ColumnsOfPredicate<Callable>::type>::call(callable, row);
  } else {
    return false;  // Unreachable: the caller has already failed to compile.
  }
}

// What a term and a predicate both have to satisfy. Skipped when the rows are not an index
// set, so that stays the only diagnostic.
template <typename Callable, typename Row>
consteval void checkColumnCallable() {
  if constexpr (isIndexSetRow<Row>()) {
    static_assert(no_indirect_call<Callable>,
                  "A bare function name decays to a function pointer and std::function "
                  "type-erases; either is an indirect call per row. Pass a lambda or a "
                  "function object.");
    static_assert(ColumnsOfPredicate<Callable>::known,
                  "Cannot read this callable's parameter list. Give it one concrete "
                  "signature: `[](auto, auto)` does not say which columns it reads. A "
                  "callable with a template operator() specializes PredicateColumns "
                  "instead.");
    static_assert(PlacementOf<Callable, Row>::all_named,
                  "Take the columns themselves: `[](MachineCol m, ItemCol i)`, not "
                  "`[](const Machine* m, const Item* i)`. Col converts to its value "
                  "implicitly, so only the parameter list changes.");
    static_assert(PlacementOf<Callable, Row>::all_present, "This callable names a column these rows do not have.");
    static_assert(
        !ColumnsOfPredicate<Callable>::known || std::tuple_size_v<typename ColumnsOfPredicate<Callable>::type> != 0,
        "This callable reads no columns, so it would be the same for every row.");
  }
}

}  // namespace dreamor::internal
