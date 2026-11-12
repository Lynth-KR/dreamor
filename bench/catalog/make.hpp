// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/make.hpp - the make family: a set generated, variables made on it, and a set joined.
//
// The family measures apart what the other families measure together as build:
// - collect: the rows. Each k1 comes with each entity of each list that the k1 offers. If the cell is sparse, only the
//   rows that the predicate keeps are rows.
// - vars: a variable for each row, unnamed, with the row's tag as its upper bound.
// - vars_named: the same, each variable named x_<id>_<id>..., as a model names its variables.
// - join: the same rows again, as A joined with B on k1. A holds every column but the period, and B holds k1 and the
//   period. The join makes the rows that collect makes, so the checks of the two phases agree.
// A and B are made before any phase, and are not measured. At k = 1 a row is its k1 alone, with no period to join on,
// so the family has no join.
//
// dreamor's sets and VarTable are compared with nested loops into a std::vector, a variable for each row, and names
// written with std::to_chars. That code is the same in every hand-written arm. Each arm writes the join as a
// hand-coder does:
// - identity and mixed group the periods of B by k1, through a std::unordered_map from the id of k1.
// - sort sorts B by the id of k1, and searches it with std::equal_range.
// - map, the usual code, keeps a std::map from k1 to its periods.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
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

namespace make {

using catalog::keyOf;
using catalog::MixedHash;
using catalog::ubOf;

// Returns the check of one row. The check of collect and of join is the sum over the rows, so the order of the rows
// does not count.
template <typename Serials>
inline std::uint64_t rowCheck(const Serials& serials, std::size_t k) {
  return keyOf(serials, k) | 1;
}

}  // namespace make
