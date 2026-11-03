// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A dependent source returning a fresh vector: checked on every call.
// expect: makeSet: dependent source 2 returned one element twice, at 0 and 1
#include "prelude.hpp"

int main() try {
  const std::vector<std::size_t> is{1, 2};
  const auto js_of = [](cI i) { return std::vector<std::size_t>{i.get(), i.get()}; };
  return static_cast<int>(dreamor::makeSet<cI, cJ>(is, js_of).collect().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
