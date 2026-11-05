// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// File: interval.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// Integer intervals as makeSet sources - AMPL's first..last, and its open and half-open kin:
//
//     makeSet<cCut>(interval::closed(0, 160))        [0, 160]   AMPL: 0..160
//     makeSet<cPeriod>(interval::open(0, T))         (0, T)     the periods strictly between
//     makeSet<cItem>(interval::closedOpen(0, n))     [0, n)     n of them, from 0
//     makeSet<cPeriod>(interval::openClosed(0, T))   (0, T]
//
// Each says the bounds the way the model thinks them; the four make the same kind of source. An
// interval whose ends cross holds nothing, as AMPL's 1..0. Its values take the column's integer
// type whatever the ends' type - 0 and 160 fill a std::size_t column - and a debug build stops on
// one the column cannot hold. Its ends are integers: closed(0.0, 160.0) does not compile. It is a
// view of values, so it is handed to makeSet as it is.
#pragma once

#include <concepts>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>

#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/logger.hpp"

namespace dreamor::interval {

namespace detail {

// [first, last): the values as IntervalPoints, in the type the two ends meet in.
template <std::integral T>
[[nodiscard]] auto points(T first, T last) {
  return std::views::iota(first, last < first ? first : last) |
         std::views::transform([](T value) { return internal::IntervalPoint<T>{value}; });
}

// The ends in their common type; a debug build stops where one does not fit it (a negative end
// beside an unsigned one).
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

// The value after last, for an interval closed at last.
template <std::integral T>
[[nodiscard]] T after(T last) {
#ifndef NDEBUG
  if (last == std::numeric_limits<T>::max()) {
    DREAMOR_DEBUG_FATAL("interval: closed at its integer type's largest value - a broken contract.");
  }
#endif
  return static_cast<T>(last + 1);
}

}  // namespace detail

// [first, last]: first, first + 1, ..., last.
template <std::integral A, std::integral B>
[[nodiscard]] auto closed(A first, B last) {
  const auto [lo, hi] = detail::ends(first, last);
  return hi < lo ? detail::points(lo, lo) : detail::points(lo, detail::after(hi));
}

// (first, last): first + 1, ..., last - 1.
template <std::integral A, std::integral B>
[[nodiscard]] auto open(A first, B last) {
  const auto [lo, hi] = detail::ends(first, last);
  return lo < hi ? detail::points(static_cast<decltype(lo)>(lo + 1), hi) : detail::points(lo, lo);
}

// [first, last): first, ..., last - 1.
template <std::integral A, std::integral B>
[[nodiscard]] auto closedOpen(A first, B last) {
  const auto [lo, hi] = detail::ends(first, last);
  return detail::points(lo, hi);
}

// (first, last]: first + 1, ..., last.
template <std::integral A, std::integral B>
[[nodiscard]] auto openClosed(A first, B last) {
  const auto [lo, hi] = detail::ends(first, last);
  return lo < hi ? detail::points(static_cast<decltype(lo)>(lo + 1), detail::after(hi)) : detail::points(lo, lo);
}

// Ends that are not integers: the four match exactly, and stop on one sentence as soon as one is
// chosen, rather than on "no matching function".
namespace detail {
template <typename A, typename B>
concept NotIntegerEnds = !(std::integral<A> && std::integral<B>);

// The return type is deduced, so the call instantiates this body before anything uses the result,
// and the sentence is the first error; the empty interval it returns raises no second one.
template <typename A, typename B>
[[nodiscard]] auto integerEnds() {
  static_assert(internal::always_false<A, B>,
                "An interval counts integers: interval::closed(0, 160), not closed(0.0, 160.0). Its ends are "
                "integers of any integer type; its values take the column's.");
  return points(0, 0);
}
}  // namespace detail

template <typename A, typename B>
  requires detail::NotIntegerEnds<A, B>
auto closed(A /*first*/, B /*last*/) {
  return detail::integerEnds<A, B>();
}
template <typename A, typename B>
  requires detail::NotIntegerEnds<A, B>
auto open(A /*first*/, B /*last*/) {
  return detail::integerEnds<A, B>();
}
template <typename A, typename B>
  requires detail::NotIntegerEnds<A, B>
auto closedOpen(A /*first*/, B /*last*/) {
  return detail::integerEnds<A, B>();
}
template <typename A, typename B>
  requires detail::NotIntegerEnds<A, B>
auto openClosed(A /*first*/, B /*last*/) {
  return detail::integerEnds<A, B>();
}

}  // namespace dreamor::interval
