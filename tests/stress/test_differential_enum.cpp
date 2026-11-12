// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) with enum columns over 2-byte integers beside 8-byte ones: the two enums
// share 8 bytes, rows of 16 bytes, joined rows of 24. The enums are the second column, the lateral column and the one
// the join shares; variables are named by their underlying integers.
#include <cstddef>
#include <cstdint>

#include "differential.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

enum class Level : std::uint16_t {};

struct Columns {
  DREAMOR_VALUE_COLUMN(A, std::size_t);
  DREAMOR_VALUE_COLUMN(B, Level);
  DREAMOR_VALUE_COLUMN(C, Level);
  DREAMOR_VALUE_COLUMN(D, std::size_t);
};

}  // namespace

int main() try { return differential::run<Columns>("differential_enum"); } catch (...) {
  return ::uncaught();
}
