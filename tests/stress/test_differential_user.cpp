// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) with columns of a type of the user's, differential::Code, found by its own
// == and std::hash - a hash that reads half of the value, so four values share each hash - beside a std::size_t: rows
// of 32 bytes, joined rows of 40. Variables are named by the Code's primary key.
#include <cstddef>

#include "differential.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

struct Columns {
  DREAMOR_VALUE_COLUMN(A, differential::Code);
  DREAMOR_VALUE_COLUMN(B, std::size_t);
  DREAMOR_VALUE_COLUMN(C, differential::Code);
  DREAMOR_VALUE_COLUMN(D, differential::Code);
};

}  // namespace

int main() try { return differential::run<Columns>("differential_user"); } catch (...) {
  return ::uncaught();
}
