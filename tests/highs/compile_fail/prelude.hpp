// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include <cstddef>
#include <vector>
#include "Highs.h"
#include "dreamor/highs.hpp"
#include "dreamor/index_set.hpp"
namespace hs = dreamor::highs;

DREAMOR_VALUE_COLUMN(cI, std::size_t);
DREAMOR_VALUE_COLUMN(cJ, std::size_t);
inline const std::vector<std::size_t> I{1, 2};

inline auto IJ() {
  return dreamor::index_set::relational::from<cI>(I).cross<cJ>(I).build();
}
