// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
using cI = dreamor::Col<std::size_t, "i">;
using cJ = dreamor::Col<std::size_t, "j">;
using cK = dreamor::Col<std::size_t, "k">;  // a column the sets below do not have
inline const std::vector<std::size_t> I{1, 2};
inline auto IJ() {
  return dreamor::makeSet<cI, cJ>(I, I).build();
}
