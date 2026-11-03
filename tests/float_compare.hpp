// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Exact floating-point comparisons for the tests. A test here checks that a value comes back
// exactly as it went in - a bound, a stub's solution - so == is the right comparison; naming it
// keeps -Wfloat-equal on everywhere else, the library included.
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
[[nodiscard]] constexpr bool exactlyEqual(double a, double b) noexcept {
  return a == b;
}
[[nodiscard]] constexpr bool isExactlyZero(double x) noexcept {
  return x == 0.0;
}
#pragma GCC diagnostic pop
