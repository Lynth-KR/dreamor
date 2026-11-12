// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Values computed over the rows of a set without a loop or a new set: allOf, anyOf, noneOf, countIf, maxOf, minOf,
// sumValue, maxElement, minElement and findIf.
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

#include "dreamor/internal/base/unreachable.hpp"
#include "dreamor/internal/index_set/call.hpp"
#include "dreamor/logger.hpp"

namespace dreamor::internal::inline index_set {

template <typename Rows>
using RowOf = std::remove_cvref_t<std::ranges::range_reference_t<Rows>>;

// Checks the rows and the callable for every function below, and returns whether the rows are rows of columns. Each
// function reads its rows only if this returns true. A compiler that reports errors in another order then still shows
// the message of this check first.
template <typename Rows, typename Callable>
consteval bool checkRowsAndCallable() {
  static_assert(std::ranges::input_range<Rows>,
                "allOf, anyOf, noneOf, countIf, maxOf, minOf, sumValue, maxElement, minElement and findIf take rows: "
                "a set, a group's rows (a RowSlice), or a range of them.");
  if constexpr (std::ranges::input_range<Rows>) {
    static_assert(isIndexSetRow<RowOf<Rows>>(),
                  "These rows are not rows of columns: pass a set, a group's rows, or a range of std::tuple<Cols...>, "
                  "so the callable can take the columns it reads.");
    if constexpr (isIndexSetRow<RowOf<Rows>>()) {
      checkColumnCallable<Callable, RowOf<Rows>>();
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

// Checks a plain container that the template argument names as one column, as sumExpr<C> also accepts. If the check
// passes, the caller wraps the container with namedAsOneColumn, which makes each element a row as it is read.
template <typename Container, typename... Columns>
consteval bool checkOneColumnContainer() {
  static_assert(sizeof...(Columns) == 1 && (column<Columns> && ...),
                "A plain container is one column: name it - sumValue<cItem>(items, weight), not "
                "sumValue<Ref<Item>>(items, weight) nor two columns; several columns are a set, built first.");
  if constexpr (sizeof...(Columns) == 1 && (column<Columns> && ...)) {
    static_assert((refsFromTheirVector<Container, Columns>() && ...), DREAMOR_INTERNAL_REFS_FROM_THEIR_VECTOR_MESSAGE);
    return (refsFromTheirVector<Container, Columns>() && ...);
  } else {
    return false;
  }
}

// maxElement, minElement and findIf return the address of a row, so the rows must outlive the call.
// std::ranges::max_element follows the same rule: for a temporary range, it returns std::ranges::dangling.
template <typename Rows>
consteval void checkRowsOutliveTheCall() {
  static_assert(std::is_lvalue_reference_v<Rows> || std::ranges::borrowed_range<Rows>,
                "maxElement, minElement and findIf hand back a row by its address: name the set first - const auto "
                "sLATE = sORDERS.where(is_late); - as the row would go with a temporary set.");
  static_assert(std::is_lvalue_reference_v<std::ranges::range_reference_t<Rows>>,
                "maxElement, minElement and findIf hand back a row by its address, and these rows are made as they "
                "are read: iterate a set or a group's rows.");
}

// The comparison that makes a smaller value the better one, with < alone, as std::ranges::min asks of a type: a
// candidate is better than the best so far if candidate < best.
struct SmallerIsBetter {
  template <typename A, typename B>
  [[nodiscard]] constexpr bool operator()(const A& best, const B& candidate) const {
    return std::less<>{}(candidate, best);
  }
};

// Returns the first row with the largest value if Better is std::less<>, or the first row with the smallest value if
// Better is SmallerIsBetter. Returns nullptr if there are no rows. The value of the first row starts the search, so
// the value's type need not be default-constructible.
template <typename Better, typename Rows, typename Value>
[[nodiscard]] const RowOf<Rows>* extremeRow(Rows& rows, const Value& value) {
  auto at        = std::ranges::begin(rows);
  const auto end = std::ranges::end(rows);
  if (at == end) { return nullptr; }
  const RowOf<Rows>* best = std::addressof(*at);
  auto best_value         = callColumnsOf(value, *at);
  for (++at; at != end; ++at) {
    const auto candidate = callColumnsOf(value, *at);
    if (Better{}(best_value, candidate)) {
      best       = std::addressof(*at);
      best_value = candidate;
    }
  }
  return best;
}

/// @contract aggregate_rows_not_empty: The `rows` argument of `maxOf` or `minOf` is not empty.

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

// Returns an iterator to the first row for which pred is true, or the end iterator if there is none.
template <typename Rows, typename Pred>
[[nodiscard]] auto findRow(Rows& rows, const Pred& pred) {
  auto at        = std::ranges::begin(rows);
  const auto end = std::ranges::end(rows);
  while (at != end && !static_cast<bool>(callColumnsOf(pred, *at))) {
    ++at;
  }
  return at;
}

}  // namespace dreamor::internal::inline index_set

namespace dreamor {

/// @section Values over a set
///
/// The functions of this section compute a value from the rows of a set, such as a count, a maximum or a sum. They
/// need no loop in the model, and they make no new set. Each function takes the rows and a column callable. For each
/// row that the function reads, it calls the callable with the columns that the callable reads.
///
/// The rows are one of these:
///
/// - a set;
/// - a group's rows (a `RowSlice`);
/// - a range of rows.
///
/// `allOf`, `anyOf`, `noneOf`, `countIf`, `maxOf`, `minOf` and `sumValue` also take a plain container, such as a
/// `std::vector` or an `EntityVector`, if one column is given as the template argument. Each element of the container
/// then fills that column, as an element of a source fills a column in `index_set::relational::from`, and makes one
/// row. `maxElement`, `minElement` and `findIf` return a pointer to a row, so they do not take a plain container.
///
/// `allOf`, `anyOf`, `noneOf` and `findIf` stop reading rows as soon as the result is known. The other functions read
/// every row.
///
/// The results are values computed from data, not expressions for a solver. For a sum that a solver reads, use
/// `sumExpr`, in the Sums section.
///
/// Guide: guide/values-over-a-set.md

// Each function takes its rows as Rows&& and reads them as an lvalue, as std::ranges::all_of and the other range
// algorithms do: the rows are read, not consumed. The rows are forwarded only to namedAsOneColumn, which may have to
// hold a temporary container. maxElement, minElement and findIf use the value category to reject a temporary set.
// NOLINTBEGIN(cppcoreguidelines-missing-std-forward): the rows are read, not consumed
/// Checks whether `pred` is true for every row of `rows`.
///
/// `allOf` stops at the first row for which `pred` is false.
///
/// Takes: If `Columns` is empty, `rows` must be a set, a group's rows, or a range of rows. If `Columns` names a
/// column, `rows` must be a container whose elements fill that column. `Columns` must name at most one column.
/// `pred` must be a column callable over the columns of the rows, and its result must convert to `bool`.
/// Returns: True if `pred` is true for every row, or if `rows` is empty; false otherwise.
/// Contract: column_integer_in_range
template <typename... Columns, typename Rows, typename Pred>
[[nodiscard]] bool allOf(Rows&& rows, const Pred& pred) {
  if constexpr (sizeof...(Columns) != 0) {
    if constexpr (internal::checkOneColumnContainer<Rows, Columns...>()) {
      return allOf(internal::namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), pred);
    } else {
      return false;  // checkOneColumnContainer has reported the error.
    }
  } else if constexpr (internal::checkRowsAndCallable<Rows, Pred>()) {
    const auto holds = [&pred](const auto& row) { return static_cast<bool>(internal::callColumnsOf(pred, row)); };
    return std::ranges::all_of(rows, holds);
  } else {
    return false;  // checkRowsAndCallable has reported the error.
  }
}

/// Checks whether `pred` is true for at least one row of `rows`.
///
/// `anyOf` stops at the first row for which `pred` is true.
///
/// Takes: If `Columns` is empty, `rows` must be a set, a group's rows, or a range of rows. If `Columns` names a
/// column, `rows` must be a container whose elements fill that column. `Columns` must name at most one column.
/// `pred` must be a column callable over the columns of the rows, and its result must convert to `bool`.
/// Returns: True if `pred` is true for at least one row; false otherwise.
/// Contract: column_integer_in_range
template <typename... Columns, typename Rows, typename Pred>
[[nodiscard]] bool anyOf(Rows&& rows, const Pred& pred) {
  if constexpr (sizeof...(Columns) != 0) {
    if constexpr (internal::checkOneColumnContainer<Rows, Columns...>()) {
      return anyOf(internal::namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), pred);
    } else {
      return false;  // checkOneColumnContainer has reported the error.
    }
  } else if constexpr (internal::checkRowsAndCallable<Rows, Pred>()) {
    return internal::findRow(rows, pred) != std::ranges::end(rows);
  } else {
    return false;  // checkRowsAndCallable has reported the error.
  }
}

/// Checks whether `pred` is false for every row of `rows`.
///
/// `noneOf` stops at the first row for which `pred` is true.
///
/// Takes: If `Columns` is empty, `rows` must be a set, a group's rows, or a range of rows. If `Columns` names a
/// column, `rows` must be a container whose elements fill that column. `Columns` must name at most one column.
/// `pred` must be a column callable over the columns of the rows, and its result must convert to `bool`.
/// Returns: True if `pred` is false for every row, or if `rows` is empty; false otherwise.
/// Contract: column_integer_in_range
template <typename... Columns, typename Rows, typename Pred>
[[nodiscard]] bool noneOf(Rows&& rows, const Pred& pred) {
  if constexpr (sizeof...(Columns) != 0) {
    if constexpr (internal::checkOneColumnContainer<Rows, Columns...>()) {
      return noneOf(internal::namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), pred);
    } else {
      return false;  // checkOneColumnContainer has reported the error.
    }
  } else if constexpr (internal::checkRowsAndCallable<Rows, Pred>()) {
    return internal::findRow(rows, pred) == std::ranges::end(rows);
  } else {
    return false;  // checkRowsAndCallable has reported the error.
  }
}

/// Returns the number of rows of `rows` for which `pred` is true.
///
/// `countIf` calls `pred` once for every row.
///
/// Takes: If `Columns` is empty, `rows` must be a set, a group's rows, or a range of rows. If `Columns` names a
/// column, `rows` must be a container whose elements fill that column. `Columns` must name at most one column.
/// `pred` must be a column callable over the columns of the rows, and its result must convert to `bool`.
/// Contract: column_integer_in_range
template <typename... Columns, typename Rows, typename Pred>
[[nodiscard]] std::size_t countIf(Rows&& rows, const Pred& pred) {
  if constexpr (sizeof...(Columns) != 0) {
    if constexpr (internal::checkOneColumnContainer<Rows, Columns...>()) {
      return countIf(internal::namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), pred);
    } else {
      return std::size_t{0};  // checkOneColumnContainer has reported the error.
    }
  } else if constexpr (internal::checkRowsAndCallable<Rows, Pred>()) {
    std::size_t count = 0;
    for (const auto& row : rows) {
      if (static_cast<bool>(internal::callColumnsOf(pred, row))) { ++count; }
    }
    return count;
  } else {
    return std::size_t{0};  // checkRowsAndCallable has reported the error.
  }
}

/// Returns the largest of the values that `value` returns for the rows of `rows`.
///
/// `maxOf` calls `value` once for every row and compares the results with `<`. The result has the type that `value`
/// returns, without `const` or a reference. For rows that may be empty, use `maxElement`, which returns a null
/// pointer if there are no rows.
///
/// Takes: If `Columns` is empty, `rows` must be a set, a group's rows, or a range of rows. If `Columns` names a
/// column, `rows` must be a container whose elements fill that column. `Columns` must name at most one column.
/// `value` must be a column callable over the columns of the rows. The results of `value` must be comparable with `<`.
/// Contract: aggregate_rows_not_empty, column_integer_in_range
template <typename... Columns, typename Rows, typename Value>
[[nodiscard]] auto maxOf(Rows&& rows, const Value& value) {
  if constexpr (sizeof...(Columns) != 0) {
    if constexpr (internal::checkOneColumnContainer<Rows, Columns...>()) {
      return maxOf(internal::namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), value);
    } else {
      return 0.0;  // checkOneColumnContainer has reported the error.
    }
  } else if constexpr (internal::checkRowsAndCallable<Rows, Value>()) {
    return internal::extremeValue<std::less<>>(rows, value, "maxOf");
  } else {
    return 0.0;  // checkRowsAndCallable has reported the error.
  }
}

/// Returns the smallest of the values that `value` returns for the rows of `rows`.
///
/// `minOf` calls `value` once for every row and compares the results with `<`. The result has the type that `value`
/// returns, without `const` or a reference. For rows that may be empty, use `minElement`, which returns a null
/// pointer if there are no rows.
///
/// Takes: If `Columns` is empty, `rows` must be a set, a group's rows, or a range of rows. If `Columns` names a
/// column, `rows` must be a container whose elements fill that column. `Columns` must name at most one column.
/// `value` must be a column callable over the columns of the rows. The results of `value` must be comparable with `<`.
/// Contract: aggregate_rows_not_empty, column_integer_in_range
template <typename... Columns, typename Rows, typename Value>
[[nodiscard]] auto minOf(Rows&& rows, const Value& value) {
  if constexpr (sizeof...(Columns) != 0) {
    if constexpr (internal::checkOneColumnContainer<Rows, Columns...>()) {
      return minOf(internal::namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), value);
    } else {
      return 0.0;  // checkOneColumnContainer has reported the error.
    }
  } else if constexpr (internal::checkRowsAndCallable<Rows, Value>()) {
    return internal::extremeValue<internal::SmallerIsBetter>(rows, value, "minOf");
  } else {
    return 0.0;  // checkRowsAndCallable has reported the error.
  }
}

/// Returns the sum of the values that `value` returns for the rows of `rows`.
///
/// `sumValue` starts from a value-initialized result and adds the value of each row with `+=`. The result has the
/// type that `value` returns, without `const` or a reference. For example, if `value` returns `int`, the sum is an
/// `int`. The sum is a value computed from data. For a sum that a solver reads, use `sumExpr`.
///
/// Takes: If `Columns` is empty, `rows` must be a set, a group's rows, or a range of rows. If `Columns` names a
/// column, `rows` must be a container whose elements fill that column. `Columns` must name at most one column.
/// `value` must be a column callable over the columns of the rows. The result type of `value` must be
/// default-constructible and must support `+=`.
/// Returns: A value-initialized result if `rows` is empty, which is 0 for a number.
/// Contract: column_integer_in_range
template <typename... Columns, typename Rows, typename Value>
[[nodiscard]] auto sumValue(Rows&& rows, const Value& value) {
  if constexpr (sizeof...(Columns) != 0) {
    if constexpr (internal::checkOneColumnContainer<Rows, Columns...>()) {
      return sumValue(internal::namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), value);
    } else {
      return 0.0;  // checkOneColumnContainer has reported the error.
    }
  } else if constexpr (internal::checkRowsAndCallable<Rows, Value>()) {
    using Result = std::remove_cvref_t<decltype(internal::callColumnsOf(value, *std::ranges::begin(rows)))>;
    Result total{};
    for (const auto& row : rows) {
      total += internal::callColumnsOf(value, row);
    }
    return total;
  } else {
    return 0.0;  // checkRowsAndCallable has reported the error.
  }
}

/// Returns a pointer to the row of `rows` for which `value` returns the largest value.
///
/// `maxElement` calls `value` once for every row and compares the results with `<`. If several rows have the largest
/// value, the pointer refers to the first of them. `std::ranges::max_element` chooses the same row.
///
/// Takes: `rows` must be a set, a group's rows, or a range of rows. `rows` must be an lvalue or a borrowed range, so
/// a temporary set does not compile. Reading `rows` must yield references to stored rows, not rows made as they are
/// read. `value` must be a column callable over the columns of the rows. The results of `value` must be comparable
/// with `<`.
/// Returns: A null pointer if `rows` is empty. A pointer that is not null refers to the row where it is stored, not
/// to a copy.
template <typename Rows, typename Value>
[[nodiscard]] auto maxElement(Rows&& rows, const Value& value) {
  if constexpr (internal::checkRowsAndCallable<Rows, Value>()) {
    internal::checkRowsOutliveTheCall<Rows>();
    return internal::extremeRow<std::less<>>(rows, value);
  } else {
    return nullptr;  // checkRowsAndCallable has reported the error.
  }
}

/// Returns a pointer to the row of `rows` for which `value` returns the smallest value.
///
/// `minElement` calls `value` once for every row and compares the results with `<`. If several rows have the
/// smallest value, the pointer refers to the first of them. `std::ranges::min_element` chooses the same row.
///
/// Takes: `rows` must be a set, a group's rows, or a range of rows. `rows` must be an lvalue or a borrowed range, so
/// a temporary set does not compile. Reading `rows` must yield references to stored rows, not rows made as they are
/// read. `value` must be a column callable over the columns of the rows. The results of `value` must be comparable
/// with `<`.
/// Returns: A null pointer if `rows` is empty. A pointer that is not null refers to the row where it is stored, not
/// to a copy.
template <typename Rows, typename Value>
[[nodiscard]] auto minElement(Rows&& rows, const Value& value) {
  if constexpr (internal::checkRowsAndCallable<Rows, Value>()) {
    internal::checkRowsOutliveTheCall<Rows>();
    return internal::extremeRow<internal::SmallerIsBetter>(rows, value);
  } else {
    return nullptr;  // checkRowsAndCallable has reported the error.
  }
}

/// Returns a pointer to the first row of `rows` for which `pred` is true.
///
/// `findIf` stops at that row.
///
/// Takes: `rows` must be a set, a group's rows, or a range of rows. `rows` must be an lvalue or a borrowed range, so
/// a temporary set does not compile. Reading `rows` must yield references to stored rows, not rows made as they are
/// read. `pred` must be a column callable over the columns of the rows, and its result must convert to `bool`.
/// Returns: A null pointer if `pred` is true for no row. A pointer that is not null refers to the row where it is
/// stored, not to a copy.
template <typename Rows, typename Pred>
[[nodiscard]] auto findIf(Rows&& rows, const Pred& pred) {
  if constexpr (internal::checkRowsAndCallable<Rows, Pred>()) {
    internal::checkRowsOutliveTheCall<Rows>();
    const auto at                          = internal::findRow(rows, pred);
    const internal::RowOf<Rows>* const row = at == std::ranges::end(rows) ? nullptr : &*at;
    return row;
  } else {
    return nullptr;  // checkRowsAndCallable has reported the error.
  }
}

// NOLINTEND(cppcoreguidelines-missing-std-forward)

}  // namespace dreamor
