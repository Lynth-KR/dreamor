// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) with a text column, std::string_view, beside two of
// std::size_t: rows of 32 bytes, joined rows of 40, whose text compares by its characters, not its
// address. The text is the lateral column and the one the join shares.
#include <cstddef>
#include <string_view>

#include "differential.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

struct Columns {
  DREAMOR_VALUE_COLUMN(A, std::size_t);
  DREAMOR_VALUE_COLUMN(B, std::size_t);
  DREAMOR_VALUE_COLUMN(C, std::string_view);
  DREAMOR_VALUE_COLUMN(D, std::size_t);
};

}  // namespace

int main() try { return differential::run<Columns>("differential_str"); } catch (...) {
  return ::uncaught();
}
