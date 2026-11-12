// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/group.hpp - the group family: a set grouped by some of its columns, and a sum for each group.
//
// The family has three shapes, each named by the column that its grouping leaves out:
// - inner leaves out the period, so every group is a run of the generation order.
// - lead leaves out the first column, k1.
// - mid leaves out the second column. It runs only at k >= 3.
// The phases are the same in every arm. build makes the rows and x on them. Then, for each shape, prep_<shape> makes
// the grouping, and sum_<shape> makes one expression for each group: x over the group's rows. sum_where makes one
// expression for each inner group: x over the group's rows whose period has an even serial.
//
// dreamor's groupBy and sumExpr are compared with hand-written groupings over packed ids:
// - identity and mixed number the groups through a std::unordered_map from the group's key, with the identity hash
//   or a mixed one, and count the rows into place (CSR).
// - sort sorts (key, row) pairs.
// - map, the usual code, keeps a std::map from the entities of the key to the rows.
// identity, mixed and sort take the groups of the inner shape as runs, as a hand-coder who knows the generation order
// would. map does not.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "shared.hpp"

namespace group {

using catalog::consume;
using catalog::keyOf;
using catalog::MixedHash;
using catalog::Packer;

// Returns the positions of the columns that a shape keeps, for k columns.
inline std::vector<std::size_t> keptBy(const std::string& shape, std::size_t k) {
  std::vector<std::size_t> kept;
  for (std::size_t j = 0; j < k; ++j) {
    const bool dropped = (shape == "inner" && j == k - 1) || (shape == "lead" && j == 0) || (shape == "mid" && j == 1);
    if (!dropped) { kept.push_back(j); }
  }
  return kept;
}

inline std::vector<std::string> shapesFor(std::size_t k) {
  return k >= 3 ? std::vector<std::string>{"inner", "lead", "mid"} : std::vector<std::string>{"inner", "lead"};
}

}  // namespace group
