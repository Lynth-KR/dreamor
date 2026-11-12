// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) on integer columns of four types, signed and unsigned, 1 to 8 bytes: rows
// of 16 bytes, joined rows of 24. The signed columns hold negative values too. A key takes a plain integer of any
// type in each place and finds its row by value - an integer out of a column's range finds none, though its low bits
// are the value's - columns of different types compare by value, and a column filled from integers of another type
// joins and filters by value. A large case's columns take at most 255 values, what B holds.
#include <cstdint>

#include "differential.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

struct Columns {
  DREAMOR_VALUE_COLUMN(A, std::int16_t);
  DREAMOR_VALUE_COLUMN(B, std::uint8_t);
  DREAMOR_VALUE_COLUMN(C, std::int64_t);
  DREAMOR_VALUE_COLUMN(D, std::uint32_t);
};

static_assert(differential::kPlainIntegerKeys<Columns>);

}  // namespace

int main() try { return differential::run<Columns>("differential_signed"); } catch (...) {
  return ::uncaught();
}
