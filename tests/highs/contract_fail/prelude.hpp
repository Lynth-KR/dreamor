// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstddef>
#include <vector>
#include "Highs.h"
#include "dreamor/highs.hpp"
#include "dreamor/index_set.hpp"
namespace hs = dreamor::highs;  // NOLINT(misc-unused-alias-decls): the cases use it

using ICol = dreamor::Col<std::size_t, "i">;
inline auto sI() {
  static const std::vector<std::size_t> i{1, 2};
  return dreamor::makeSet<ICol>(i).collect();
}
