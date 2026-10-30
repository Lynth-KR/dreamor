// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <vector>
#include "Highs.h"
#include "dreamor/highs.hpp"
#include "dreamor/index_set.hpp"
namespace hs = dreamor::highs;

using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;
inline const std::vector<std::size_t> I{1, 2};
inline auto IJ() {
  return dreamor::makeSet<ICol, JCol>(I, I).collect();
}
