// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
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

#include "dreamor/internal/index_set/binding.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/element.hpp"

namespace dreamor::internal::inline index_set {

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

// A callable that needs no lookup at all given a row's position in its own set: a variable
// table, whose variables are in the set's order. A sum over a group of that set, which holds its
// rows' positions, takes the term by position. The backends' tables specialize it with
// firstRow(callable), the set's first row, and at(callable, n), the term on the row at n.
template <typename Callable>
struct TakesRowNumber : std::false_type {};

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
                  "callable with a template operator() specializes CallableColumns "
                  "instead.");
    static_assert(PlacementOf<Callable, Row>::all_named,
                  "Take the columns themselves: `[](cMachine m, cItem i)`, not "
                  "`[](const Machine* m, const Item* i)`. Col converts to its value "
                  "implicitly, so only the parameter list changes.");
    static_assert(PlacementOf<Callable, Row>::all_present, "This callable names a column these rows do not have.");
    static_assert(
        PlacementOf<Callable, Row>::no_repeated_column,
        "This callable takes the same column twice, so both parameters would get one value: give each its own "
        "column - `[](cFrom a, cTo b)` - or take the column once.");
    static_assert(
        !ColumnsOfPredicate<Callable>::known || std::tuple_size_v<typename ColumnsOfPredicate<Callable>::type> != 0,
        "This callable reads no columns, so it would be the same for every row.");
  }
}

// A VarTable's bound and a ValueTable's value are given alike: a number, or a callable over the
// row's columns.
template <typename Bound, typename Row>
consteval void checkBound() {
  if constexpr (!std::is_arithmetic_v<Bound>) { checkColumnCallable<Bound, Row>(); }
}
template <typename Bound, typename Row>
[[nodiscard]] double boundOf(const Bound& bound, const Row& row) {
  if constexpr (std::is_arithmetic_v<Bound>) {
    return static_cast<double>(bound);
  } else {
    return static_cast<double>(callColumnsOf(bound, row));
  }
}

}  // namespace dreamor::internal::inline index_set
