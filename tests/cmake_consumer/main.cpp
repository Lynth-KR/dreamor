// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Only the solver-free part, so the check needs no CPLEX and no OR-Tools.
#include <cstddef>
#include <cstdio>
#include <vector>

#include "dreamor/index_set.hpp"

DREAMOR_VALUE_COLUMN(cI, std::size_t);
DREAMOR_VALUE_COLUMN(cJ, std::size_t);

int main() {
  const std::vector<std::size_t> I{1, 2, 3}, J{10, 20};
  const auto sIJ =
      dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).where([](cI i) { return i.get() != 2; }).build();
  std::size_t walked = 0;
  for (const auto& [i, rows] : sIJ.groupBy<cI>()) {
    walked += rows.size();
  }
  const bool ok = sIJ.size() == 4 && walked == 4;
  std::printf("cmake consumer: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
