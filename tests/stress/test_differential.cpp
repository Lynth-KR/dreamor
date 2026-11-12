// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) on columns of std::size_t: rows of 24 bytes, no power
// of two, joined rows of 32. The other layouts are test_differential_*.cpp.
#include <cstddef>

#include "differential.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

struct Columns {
  DREAMOR_VALUE_COLUMN(A, std::size_t);
  DREAMOR_VALUE_COLUMN(B, std::size_t);
  DREAMOR_VALUE_COLUMN(C, std::size_t);
  DREAMOR_VALUE_COLUMN(D, std::size_t);
};

}  // namespace

int main() try { return differential::run<Columns>("differential"); } catch (...) {
  return ::uncaught();
}
