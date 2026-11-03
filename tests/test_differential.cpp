// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) on columns of std::size_t: rows of 24 bytes, no power
// of two, joined rows of 32. The other layouts are test_differential_*.cpp.
#include <cstddef>

#include "check.hpp"
#include "differential.hpp"
#include "dreamor/index_set.hpp"

namespace {

struct Columns {
  using A = dreamor::Col<std::size_t, "a">;
  using B = dreamor::Col<std::size_t, "b">;
  using C = dreamor::Col<std::size_t, "c">;
  using D = dreamor::Col<std::size_t, "d">;
};

}  // namespace

int main() try { return differential::run<Columns>("differential"); } catch (...) {
  return ::uncaught();
}
