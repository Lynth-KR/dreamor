// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) on columns of std::uint32_t: rows of 12 bytes, aligned
// to 4 only, joined rows of 16.
#include <cstdint>

#include "differential.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

struct Columns {
  DREAMOR_VALUE_COLUMN(A, std::uint32_t);
  DREAMOR_VALUE_COLUMN(B, std::uint32_t);
  DREAMOR_VALUE_COLUMN(C, std::uint32_t);
  DREAMOR_VALUE_COLUMN(D, std::uint32_t);
};

}  // namespace

int main() try { return differential::run<Columns>("differential_u32"); } catch (...) {
  return ::uncaught();
}
