// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: maxOf has no value to return - a broken contract: the rows are empty
#include "prelude.hpp"

int main() try {
  using cN         = dreamor::Col<std::size_t, "n">;
  const auto value = [](cN n) { return n.get(); };
  const dreamor::IndexSet<cN> none;
  return static_cast<int>(dreamor::maxOf(none, value));
} catch (...) {
  return 1;  // no message: the runner reports it
}
