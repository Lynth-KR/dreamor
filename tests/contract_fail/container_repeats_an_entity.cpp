// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: makeSet: source 1 holds one element twice, at 1 and 2
#include "prelude.hpp"

int main() try {
  const Entity a{0};
  const Entity b{1};
  const std::vector<const Entity*> es{&a, &b, &b};
  return static_cast<int>(dreamor::makeSet<cE>(es).build().size());
} catch (...) {
  return 1;  // no message: the runner reports it
}
