// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include <cstddef>
#include <vector>
#include "dreamor/index_set.hpp"
#include "dreamor/mathopt.hpp"
namespace math_opt = ::operations_research::math_opt;

using cI = dreamor::Col<std::size_t, "i">;
using cJ = dreamor::Col<std::size_t, "j">;
inline const std::vector<std::size_t> I{1, 2};
inline auto IJ() {
  return dreamor::makeSet<cI, cJ>(I, I).build();
}
