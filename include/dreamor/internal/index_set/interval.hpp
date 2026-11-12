// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Integer intervals, closed, open or half-open: views of integers that fill a value column.
#pragma once

#include <concepts>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>

#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/logger.hpp"

namespace dreamor::interval::detail {

// An end of an interval: an integer, or a column that holds an integer.
template <typename A>
concept integer_end = std::integral<A> || (internal::column<A> && std::integral<typename A::value_type>);

template <integer_end A>
[[nodiscard]] auto endValue(A end) {
  if constexpr (std::integral<A>) {
    return end;
  } else {
    return end.get();
  }
}

// The values in [first, last) as IntervalPoints, in the common type of the two ends.
template <std::integral T>
[[nodiscard]] auto points(T first, T last) {
  return std::views::iota(first, last < first ? first : last) |
         std::views::transform([](T value) { return internal::IntervalPoint<T>{value}; });
}

/// @contract interval_ends_in_common_type: The common type of the two ends can hold the value of each end. For
/// example, the ends `-1` and a `std::size_t` break this contract: their common type is `std::size_t`, which cannot
/// hold `-1`.

// Converts the ends to their common type.
template <std::integral A, std::integral B>
[[nodiscard]] auto ends(A first, B last) {
  using T = std::common_type_t<A, B>;
#ifndef NDEBUG
  if (!std::in_range<T>(first) || !std::in_range<T>(last)) {
    DREAMOR_DEBUG_FATAL("interval: an end its interval's integer type cannot hold - a broken contract.");
  }
#endif
  return std::pair<T, T>{static_cast<T>(first), static_cast<T>(last)};
}

/// @contract interval_closed_below_largest: If an interval includes its last end and is not empty, the last end is
/// less than the largest value of the common type of the ends.

// The value after last, for an interval that includes last.
template <std::integral T>
[[nodiscard]] T after(T last) {
#ifndef NDEBUG
  if (last == std::numeric_limits<T>::max()) {
    DREAMOR_DEBUG_FATAL("interval: closed at its integer type's largest value - a broken contract.");
  }
#endif
  return static_cast<T>(last + 1);
}

// Ends that are not integers. The four functions that take such ends match exactly, so the call fails with one
// message instead of "no matching function".
template <typename A, typename B>
concept not_integer_ends = !(integer_end<A> && integer_end<B>);

// The return type is deduced, so a call instantiates this body before anything uses the result, and the message is
// the first error. The empty interval that the body returns causes no second error.
template <typename A, typename B>
[[nodiscard]] auto integerEnds() {
  static_assert(internal::always_false<A, B>,
                "An interval counts integers: interval::closed(0, 160), not closed(0.0, 160.0). Its ends are "
                "integers of any integer type, or columns of them - closed(t, T); its values take the column's.");
  return points(0, 0);
}

}  // namespace dreamor::interval::detail

namespace dreamor::interval {

/// @section Intervals
///
/// The functions of this section make intervals: views of consecutive integers, used as sources of a column of
/// integers. An interval can be passed to `index_set::relational::from`, `cross` or `in`, or returned by the callable
/// of `lateral` or `in`.
///
/// Creating an interval takes constant time, and its values are computed as they are read. The two ends are first
/// converted to their common type. Each value is converted to the integer type of the column that it fills.
///
/// Guide: guide/relational-algebra.md

/// Returns a view of the integers from `first` to `last`, both included: `first, first + 1, ..., last`.
///
/// Takes: `first` and `last` must each be an integer of any integer type, or a column that holds an integer.
/// Returns: An empty view if `last` is less than `first`. The elements are of an internal type, and each element is
/// converted to the integer type of the column that it fills.
/// Contract: interval_ends_in_common_type, interval_closed_below_largest
template <detail::integer_end A, detail::integer_end B>
[[nodiscard]] auto closed(A first, B last) {
  const auto [lo, hi] = detail::ends(detail::endValue(first), detail::endValue(last));
  return hi < lo ? detail::points(lo, lo) : detail::points(lo, detail::after(hi));
}

/// Returns a view of the integers between `first` and `last`, neither included: `first + 1, ..., last - 1`.
///
/// Takes: `first` and `last` must each be an integer of any integer type, or a column that holds an integer.
/// Returns: An empty view if `last` is less than or equal to `first + 1`. The elements are of an internal type, and
/// each element is converted to the integer type of the column that it fills.
/// Contract: interval_ends_in_common_type
template <detail::integer_end A, detail::integer_end B>
[[nodiscard]] auto open(A first, B last) {
  const auto [lo, hi] = detail::ends(detail::endValue(first), detail::endValue(last));
  return lo < hi ? detail::points(static_cast<decltype(lo)>(lo + 1), hi) : detail::points(lo, lo);
}

/// Returns a view of the integers from `first` to `last`, with `first` included and `last` excluded: `first, ...,
/// last - 1`.
///
/// Takes: `first` and `last` must each be an integer of any integer type, or a column that holds an integer.
/// Returns: An empty view if `last` is less than or equal to `first`. The elements are of an internal type, and each
/// element is converted to the integer type of the column that it fills.
/// Contract: interval_ends_in_common_type
template <detail::integer_end A, detail::integer_end B>
[[nodiscard]] auto closedOpen(A first, B last) {
  const auto [lo, hi] = detail::ends(detail::endValue(first), detail::endValue(last));
  return detail::points(lo, hi);
}

/// Returns a view of the integers from `first` to `last`, with `first` excluded and `last` included: `first + 1,
/// ..., last`.
///
/// Takes: `first` and `last` must each be an integer of any integer type, or a column that holds an integer.
/// Returns: An empty view if `last` is less than or equal to `first`. The elements are of an internal type, and each
/// element is converted to the integer type of the column that it fills.
/// Contract: interval_ends_in_common_type, interval_closed_below_largest
template <detail::integer_end A, detail::integer_end B>
[[nodiscard]] auto openClosed(A first, B last) {
  const auto [lo, hi] = detail::ends(detail::endValue(first), detail::endValue(last));
  return lo < hi ? detail::points(static_cast<decltype(lo)>(lo + 1), detail::after(hi)) : detail::points(lo, lo);
}

/// @hidden - exists only to reject the call with a message: the ends are not integers
template <typename A, typename B>
  requires detail::not_integer_ends<A, B>
auto closed(A /*first*/, B /*last*/) {
  return detail::integerEnds<A, B>();
}

/// @hidden - exists only to reject the call with a message: the ends are not integers
template <typename A, typename B>
  requires detail::not_integer_ends<A, B>
auto open(A /*first*/, B /*last*/) {
  return detail::integerEnds<A, B>();
}

/// @hidden - exists only to reject the call with a message: the ends are not integers
template <typename A, typename B>
  requires detail::not_integer_ends<A, B>
auto closedOpen(A /*first*/, B /*last*/) {
  return detail::integerEnds<A, B>();
}

/// @hidden - exists only to reject the call with a message: the ends are not integers
template <typename A, typename B>
  requires detail::not_integer_ends<A, B>
auto openClosed(A /*first*/, B /*last*/) {
  return detail::integerEnds<A, B>();
}

}  // namespace dreamor::interval
