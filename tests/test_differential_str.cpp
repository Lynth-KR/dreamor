// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The differential test (differential.hpp) with a text column, std::string, beside two of
// std::size_t: rows that own memory, of 48 bytes with libstdc++, joined rows of 56. The text is the
// dependent column and the one the join shares.
#include <cstddef>
#include <string>

#include "check.hpp"
#include "differential.hpp"
#include "dreamor/index_set.hpp"

namespace {

struct Columns {
  using A = dreamor::Col<std::size_t, "a">;
  using B = dreamor::Col<std::size_t, "b">;
  using C = dreamor::Col<std::string, "c">;
  using D = dreamor::Col<std::size_t, "d">;
};

}  // namespace

int main() try { return differential::run<Columns>("differential_str"); } catch (...) {
  return ::uncaught();
}
