// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Calls a column callable with its columns, taken from one row of a set that is already built. A chain's conditions
// and lateral sources run on rows that are not complete yet, and plan.hpp calls them instead.
#pragma once

#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/internal/index_set/binding.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/element.hpp"

namespace dreamor::internal::inline index_set {

// Checks whether Row is a row of a set: a std::tuple whose elements are all columns.
template <typename Row>
[[nodiscard]] consteval bool isIndexSetRow() {
  if constexpr (IsTuple<Row>::value) {
    return allElementsAreColumns<Row>(std::make_index_sequence<std::tuple_size_v<Row>>{});
  } else {
    return false;
  }
}

// Calls a callable with only the columns that it reads. The row is complete and each of its columns has a distinct
// type, so std::get finds each column by its type, and no positions are needed.
template <typename Cols>
struct CallColumnsOf;

template <typename... Cols>
struct CallColumnsOf<std::tuple<Cols...>> {
  template <typename Callable, typename Row>
  [[nodiscard]] static decltype(auto) call(const Callable& callable, const Row& row) {
    return callable(std::get<Cols>(row)...);
  }
};

// True for a callable that also accepts a whole row and is faster with it: a VarTable, a SolutionTable or a ParamTable
// finds a row of its own set by the row's address. Each of them specializes this trait. Every other callable is
// called with the columns that it reads.
template <typename Callable>
struct TakesWholeRow : std::false_type {};

// True for a callable that needs no lookup if it is given a row's position in its own set: a VarTable, whose
// variables are in the order of its set. A sum over a group of that set holds the positions of the group's rows, so
// the sum takes each term by its position. var_table_core.hpp specializes this trait with firstRow(table), which
// returns the set's first row, and at(table, n), which returns the term on the row at position n.
template <typename Callable>
struct TakesRowNumber : std::false_type {};

// The check on the row keeps one error for a range that is not a set: the caller, such as sumExpr or a VarTable,
// reports that error, and no further error comes from each call.
template <typename Callable, typename Row>
[[nodiscard]] decltype(auto) callColumnsOf(const Callable& callable, const Row& row) {
  if constexpr (TakesWholeRow<Callable>::value &&
                std::is_same_v<typename ColumnsOfPredicate<Callable>::type, std::remove_cvref_t<Row>>) {
    return callable(row);
  } else if constexpr (isIndexSetRow<std::remove_cvref_t<Row>>()) {
    return CallColumnsOf<typename ColumnsOfPredicate<Callable>::type>::call(callable, row);
  } else {
    return false;  // unreachable: the caller has already failed to compile
  }
}

// The requirements that every column callable must meet. The checks are skipped if the rows are not rows of a set,
// so that the error about the rows stays the only error.
template <typename Callable, typename Row>
consteval void checkColumnCallable() {
  if constexpr (isIndexSetRow<Row>()) {
    static_assert(no_indirect_call<Callable>,
                  "A bare function name or a std::function is an indirect call per row: pass a lambda or a "
                  "function object.");
    static_assert(ColumnsOfPredicate<Callable>::known, DREAMOR_INTERNAL_READ_PARAMETERS_MESSAGE);
    static_assert(PlacementOf<Callable, Row>::all_named, DREAMOR_INTERNAL_TAKE_COLUMNS_MESSAGE);
    static_assert(ColumnsOfPredicate<Callable>::no_mutable_reference, DREAMOR_INTERNAL_READ_ONLY_COLUMNS_MESSAGE);
    static_assert(PlacementOf<Callable, Row>::all_present,
                  "This callable takes a column these rows do not have: take only the rows' own columns - over "
                  "IndexSet<cMachine, cItem>, `[](cMachine m, cItem i)` or either alone.");
    static_assert(
        PlacementOf<Callable, Row>::no_repeated_column,
        "This callable takes the same column twice, so both parameters would get one value: give each its own "
        "column - `[](cFrom a, cTo b)` - or take the column once.");
    static_assert(
        !ColumnsOfPredicate<Callable>::known || std::tuple_size_v<typename ColumnsOfPredicate<Callable>::type> != 0,
        "This callable reads no columns, so it would be the same for every row: take the columns it reads - "
        "`[](cMachine m)` - or use the value itself, outside the call.");
  }
}

// True for a value that cannot be a column callable at all: a container, or a value that is not a class, such as a
// pointer or an enum. A function pointer is excluded, because no_indirect_call reports it with its own message. A
// class is reported by the check of its parameter list.
template <typename Value>
inline constexpr bool kNotCallable = !ColumnsOfPredicate<Value>::known && no_indirect_call<Value> &&
                                     (std::ranges::range<const Value&> || !std::is_class_v<Value>);

// A VarTable's bound and a ParamTable's value are given in the same way: as a number, or as a column callable that
// returns a number.
template <typename Bound, typename Row>
consteval void checkBound() {
  if constexpr (std::is_arithmetic_v<Bound>) {
    return;
  } else if constexpr (kNotCallable<Bound>) {
    static_assert(!kNotCallable<Bound>,
                  "A VarTable's bound is a number or a callable over the row's columns - "
                  "`[](cMachine m) { return m->capacity; }` - and a bound kept for each row is a ParamTable on "
                  "the set, itself such a callable; a container is neither.");
  } else {
    checkColumnCallable<Bound, Row>();
    if constexpr (ColumnsOfPredicate<Bound>::known && ColumnsOfPredicate<Bound>::no_mutable_reference &&
                  PlacementOf<Bound, Row>::all_named && PlacementOf<Bound, Row>::all_present) {
      static_assert(
          std::is_convertible_v<decltype(callColumnsOf(std::declval<const Bound&>(), std::declval<const Row&>())),
                                double>,
          "A VarTable's bound is a number, and this callable returns something else - a Ref, or an entity's "
          "column: return the number, `[](cMachine m) { return m->capacity; }`.");
    }
  }
}

template <typename Bound, typename Row>
[[nodiscard]] double boundOf(const Bound& bound, const Row& row) {
  if constexpr (std::is_arithmetic_v<Bound>) {
    return static_cast<double>(bound);
  } else if constexpr (std::is_convertible_v<decltype(callColumnsOf(bound, row)), double>) {
    return static_cast<double>(callColumnsOf(bound, row));
  } else {
    return 0;  // checkBound has already reported the error
  }
}

}  // namespace dreamor::internal::inline index_set
