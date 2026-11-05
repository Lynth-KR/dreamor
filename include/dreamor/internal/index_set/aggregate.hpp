// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// File: aggregate.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// Data over a set's rows, without a loop and without making a set: AMPL's forall, exists, card, max, min and
// sum over data, and the row std::ranges::max_element and find_if give. The callables take the columns they
// read, as where()'s predicates do; the rows are a set, a group's rows (a RowSlice), or any range of rows.
// Each stops as soon as it has its answer.
//
//     const auto quantity_of = [](cOrder o) { return o->quantity(); };
//     const auto due_by_7    = [](cItem i) { return i->dueDate() <= 7; };
//     dreamor::allOf(sITEMS, due_by_7);          // forall {i in ITEMS} due[i] <= 7
//     dreamor::countIf(sITEMS, due_by_7);        // card {i in ITEMS: due[i] <= 7}
//     dreamor::maxOf(sORDERS, quantity_of);      // max {o in ORDERS} qty[o]; ORDERS is not empty
//     dreamor::sumValue(sORDERS, quantity_of);   // sum {o in ORDERS} qty[o]
//     const auto* biggest = dreamor::maxElement(sORDERS, quantity_of);  // the row of the largest, or nullptr
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <ranges>
#include <type_traits>

#include "dreamor/internal/base/unreachable.hpp"
#include "dreamor/internal/index_set/call.hpp"
#include "dreamor/logger.hpp"

namespace dreamor {

namespace internal::inline index_set {

template <typename Rows>
using RowOf = std::remove_cvref_t<std::ranges::range_reference_t<Rows>>;

// The rows and the callable, checked once for every function below.
template <typename Rows, typename Callable>
consteval void checkRowsAndCallable() {
  static_assert(std::ranges::input_range<Rows>,
                "allOf, anyOf, noneOf, countIf, maxOf, minOf, sumValue, maxElement, minElement and findIf take rows: "
                "a set, a group's rows (a RowSlice), or a range of them.");
  if constexpr (std::ranges::input_range<Rows>) {
    static_assert(isIndexSetRow<RowOf<Rows>>(),
                  "These rows are not rows of columns: pass a set, a group's rows, or a range of std::tuple<Cols...>, "
                  "so the callable can take the columns it reads.");
    checkColumnCallable<Callable, RowOf<Rows>>();
  }
}

// maxElement, minElement and findIf hand a row back by its address, so the rows outlive the call - the rule by
// which std::ranges::max_element returns std::ranges::dangling for a temporary.
template <typename Rows>
consteval void checkRowsOutliveTheCall() {
  static_assert(std::is_lvalue_reference_v<Rows> || std::ranges::borrowed_range<Rows>,
                "maxElement, minElement and findIf hand back a row by its address: name the set first - const auto "
                "sLATE = sORDERS.where(is_late); - as the row would go with a temporary set.");
  static_assert(std::is_lvalue_reference_v<std::ranges::range_reference_t<Rows>>,
                "maxElement, minElement and findIf hand back a row by its address, and these rows are made as they "
                "are read: iterate a set or a group's rows.");
}

// The row whose value is the largest (Better = less) or the smallest (Better = greater), the first of equals.
template <typename Better, typename Rows, typename Value>
[[nodiscard]] const RowOf<Rows>* extremeRow(Rows& rows, const Value& value) {
  const RowOf<Rows>* best = nullptr;
  using Result            = std::remove_cvref_t<decltype(callColumnsOf(value, *std::ranges::begin(rows)))>;
  Result best_value{};
  for (const auto& row : rows) {
    const Result candidate = callColumnsOf(value, row);
    if (best == nullptr || Better{}(best_value, candidate)) {
      best       = &row;
      best_value = candidate;
    }
  }
  return best;
}

template <typename Better, typename Rows, typename Value>
[[nodiscard]] auto extremeValue(Rows& rows, const Value& value, [[maybe_unused]] const char* what) {
  auto at        = std::ranges::begin(rows);
  const auto end = std::ranges::end(rows);
  if (at == end) {
    DREAMOR_DEBUG_FATAL(
        "{} has no value to return - a broken contract: the rows are empty. Check with empty() first, or take the "
        "row with maxElement / minElement, which gives nullptr.",
        what);
    unreachable();
  }
  std::remove_cvref_t<decltype(callColumnsOf(value, *at))> best = callColumnsOf(value, *at);
  for (++at; at != end; ++at) {
    const auto candidate = callColumnsOf(value, *at);
    if (Better{}(best, candidate)) { best = candidate; }
  }
  return best;
}

// The first row a predicate holds for, or the end.
template <typename Rows, typename Pred>
[[nodiscard]] auto findRow(Rows& rows, const Pred& pred) {
  auto at        = std::ranges::begin(rows);
  const auto end = std::ranges::end(rows);
  while (at != end && !static_cast<bool>(callColumnsOf(pred, *at))) {
    ++at;
  }
  return at;
}

}  // namespace internal::inline index_set

// The rows are taken as Rows&& and walked as an lvalue, never forwarded - as std::ranges::all_of and the rest take
// R&&: a range is read, not consumed, and the value category tells maxElement and the like a temporary set.
// NOLINTBEGIN(cppcoreguidelines-missing-std-forward): rows are walked, not consumed, as by std::ranges
// forall: whether pred holds for every row (true for none).
template <typename Rows, typename Pred>
[[nodiscard]] bool allOf(Rows&& rows, const Pred& pred) {
  internal::checkRowsAndCallable<Rows, Pred>();
  const auto holds = [&pred](const auto& row) { return static_cast<bool>(internal::callColumnsOf(pred, row)); };
  return std::ranges::all_of(rows, holds);
}

// exists: whether pred holds for some row.
template <typename Rows, typename Pred>
[[nodiscard]] bool anyOf(Rows&& rows, const Pred& pred) {
  internal::checkRowsAndCallable<Rows, Pred>();
  return internal::findRow(rows, pred) != std::ranges::end(rows);
}

// Whether pred holds for no row.
template <typename Rows, typename Pred>
[[nodiscard]] bool noneOf(Rows&& rows, const Pred& pred) {
  internal::checkRowsAndCallable<Rows, Pred>();
  return internal::findRow(rows, pred) == std::ranges::end(rows);
}

// card {rows: pred}, with no set made.
template <typename Rows, typename Pred>
[[nodiscard]] std::size_t countIf(Rows&& rows, const Pred& pred) {
  internal::checkRowsAndCallable<Rows, Pred>();
  std::size_t count = 0;
  for (const auto& row : rows) {
    if (static_cast<bool>(internal::callColumnsOf(pred, row))) { ++count; }
  }
  return count;
}

// The largest value over the rows - AMPL's max. The rows are not empty: a precondition, as for std::ranges::max,
// which a debug build checks.
template <typename Rows, typename Value>
[[nodiscard]] auto maxOf(Rows&& rows, const Value& value) {
  internal::checkRowsAndCallable<Rows, Value>();
  return internal::extremeValue<std::less<>>(rows, value, "maxOf");
}

// The smallest value over the rows - AMPL's min; the rows are not empty, as for maxOf.
template <typename Rows, typename Value>
[[nodiscard]] auto minOf(Rows&& rows, const Value& value) {
  internal::checkRowsAndCallable<Rows, Value>();
  return internal::extremeValue<std::greater<>>(rows, value, "minOf");
}

// The sum of a value over the rows - data, not the solver's expression (that is sumExpr): 0 for none.
template <typename Rows, typename Value>
[[nodiscard]] auto sumValue(Rows&& rows, const Value& value) {
  internal::checkRowsAndCallable<Rows, Value>();
  using Result = std::remove_cvref_t<decltype(internal::callColumnsOf(value, *std::ranges::begin(rows)))>;
  Result total{};
  for (const auto& row : rows) {
    total += internal::callColumnsOf(value, row);
  }
  return total;
}

// The row with the largest value, the first of equals - std::ranges::max_element's choice - or nullptr for none.
template <typename Rows, typename Value>
[[nodiscard]] auto maxElement(Rows&& rows, const Value& value) {
  internal::checkRowsAndCallable<Rows, Value>();
  internal::checkRowsOutliveTheCall<Rows>();
  return internal::extremeRow<std::less<>>(rows, value);
}

// The row with the smallest value, the first of equals, or nullptr for none.
template <typename Rows, typename Value>
[[nodiscard]] auto minElement(Rows&& rows, const Value& value) {
  internal::checkRowsAndCallable<Rows, Value>();
  internal::checkRowsOutliveTheCall<Rows>();
  return internal::extremeRow<std::greater<>>(rows, value);
}

// The first row pred holds for, or nullptr.
template <typename Rows, typename Pred>
[[nodiscard]] auto findIf(Rows&& rows, const Pred& pred) {
  internal::checkRowsAndCallable<Rows, Pred>();
  internal::checkRowsOutliveTheCall<Rows>();
  const auto at                          = internal::findRow(rows, pred);
  const internal::RowOf<Rows>* const row = at == std::ranges::end(rows) ? nullptr : &*at;
  return row;
}

// NOLINTEND(cppcoreguidelines-missing-std-forward)

}  // namespace dreamor
