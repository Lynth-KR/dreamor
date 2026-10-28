// SPDX-License-Identifier: Apache-2.0
// Only the solver-free part, so the check needs no CPLEX and no OR-Tools.
#include <cstddef>
#include <cstdio>
#include <vector>

#include "dreamor/index_set.hpp"

using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;

int main() {
  const std::vector<std::size_t> I{1, 2, 3}, J{10, 20};
  const auto sIJ     = dreamor::makeSet<ICol, JCol>(I, J).where([](ICol i) { return i.get() != 2; }).collect();
  std::size_t walked = 0;
  for (const auto& [i, rows] : sIJ.groupBy<ICol>()) {
    walked += rows.size();
  }
  const bool ok = sIJ.size() == 4 && walked == 4;
  std::printf("cmake consumer: %s\n", ok ? "ok" : "FAILED");
  return ok ? 0 : 1;
}
