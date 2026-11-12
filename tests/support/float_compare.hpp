// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Exact floating-point comparisons for the tests. Floating-point values are compared with care: a value computed
// two ways seldom agrees to the last bit, so a computed value is compared within a tolerance (a test that solves has
// its own near()). A value that comes back exactly as it went in - a bound, a datum, a stub's solution - is compared
// exactly, and the test says so by name: a bare == on doubles reads as a slip, and -Wfloat-equal, on in every test
// build, rejects it. The name keeps the intent visible, and is the one place to change if the comparison must change
// later. exactlyEqual is ==, not a comparison of bytes: 0.0 equals -0.0, and a NaN equals nothing.
#pragma once

#include <functional>

// std::equal_to does the ==: inside the standard library's header, where -Wfloat-equal stays quiet on
// every compiler, so no compiler's own pragma is needed here.
[[nodiscard]] constexpr bool exactlyEqual(double a, double b) noexcept {
  return std::equal_to<>{}(a, b);
}

[[nodiscard]] constexpr bool isExactlyZero(double x) noexcept {
  return std::equal_to<>{}(x, 0.0);
}
