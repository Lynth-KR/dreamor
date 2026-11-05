// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#pragma once
#include <cstddef>
#include <vector>
#include "Highs.h"
#include "dreamor/highs.hpp"
#include "dreamor/index_set.hpp"
namespace hs = dreamor::highs;  // NOLINT(misc-unused-alias-decls): the cases use it

using cI = dreamor::Col<std::size_t, "i">;
inline auto sI() {
  static const std::vector<std::size_t> i{1, 2};
  return dreamor::makeSet<cI>(i).build();
}
