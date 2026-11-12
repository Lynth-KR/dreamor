// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) with text columns that own their characters, std::string, beside a view of
// text and a std::size_t: each row copies its texts, compared by their characters. The texts are the first column, the
// lateral column and the one the join shares; the joined column views its text.
#include <cstddef>
#include <string>
#include <string_view>

#include "differential.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

struct Columns {
  DREAMOR_VALUE_COLUMN(A, std::string);
  DREAMOR_VALUE_COLUMN(B, std::size_t);
  DREAMOR_VALUE_COLUMN(C, std::string);
  DREAMOR_VALUE_COLUMN(D, std::string_view);
};

}  // namespace

int main() try { return differential::run<Columns>("differential_string"); } catch (...) {
  return ::uncaught();
}
