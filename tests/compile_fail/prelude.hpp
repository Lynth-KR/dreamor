// SPDX-License-Identifier: Apache-2.0
#include <cstddef>
#include <string>
#include <vector>
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;
using KCol = dreamor::Col<std::size_t, "k">;  // a column the sets below do not have
inline const std::vector<std::size_t> I{1, 2};
inline auto IJ() {
  return dreamor::makeSet<ICol, JCol>(I, I).collect();
}
