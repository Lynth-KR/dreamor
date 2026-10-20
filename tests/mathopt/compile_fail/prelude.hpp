// SPDX-License-Identifier: Apache-2.0
#include <cstddef>
#include <vector>
#include "dreamor/index_set.hpp"
#include "dreamor/mathopt.hpp"
namespace math_opt = ::operations_research::math_opt;

using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;
inline const std::vector<std::size_t> I{1, 2};
inline auto IJ() {
  return dreamor::makeSet<ICol, JCol>(I, I).collect();
}
