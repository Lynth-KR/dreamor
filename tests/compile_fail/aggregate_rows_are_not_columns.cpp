// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// expect: These rows are not rows of columns
#include "prelude.hpp"
int main() {
  const std::vector<std::size_t> values{1, 2};
  const auto big = [](cI i) { return i.get() > 1; };
  (void)dreamor::countIf(values, big);
}
