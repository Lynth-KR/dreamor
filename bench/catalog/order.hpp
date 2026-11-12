// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/order.hpp - the order family: the neighbours of a period, and the variables there.
//
// The phases are the same in every arm:
// - build: the rows and x on them.
// - index: what the phases below go through. For dreamor, these are the row index of x, which this phase forces, and
//   the Ordered over the periods. For a hand-written arm, it is the arm's map or array. The positions of the periods,
//   which a hand-written arm keeps beside the entities, are made before any phase.
// - step: the period before the period of every row, through prev. The phase walks the order alone, with no variable.
// - lag: x of every row at the period kLag before its own, if that row exists: x[..., t - L].
// - window: x of every row over the kWindow periods up to its own, for the rows that exist. It is a rolling sum, the
//   sum over tau in t-W+1..t of x[..., tau].
//
// dreamor's Ordered over the periods is compared with the position of the period, which a hand-coder keeps beside
// each entity, and arithmetic on it. The variables are found as in lookup:
// - identity and mixed pack the ids and look them up in a std::unordered_map, with the identity hash or a mixed one.
// - array looks the packed ids up in a direct array.
// - map, the usual code, keeps one std::map from a row's entities to its variable.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "shared.hpp"

namespace order {

using catalog::MixedHash;
using catalog::Packer;
using catalog::serialsOf;
using catalog::ubOf;

inline constexpr std::ptrdiff_t kLag    = 4;
inline constexpr std::ptrdiff_t kWindow = 4;

// Returns every period that a list can hold, in serial order: the entities of the last column but the absent one.
template <typename Arm>
inline std::vector<typename Arm::Handle> periodsOf(const DataOf<Arm>& d) {
  using Handle = typename Arm::Handle;
  std::vector<Handle> periods;
  for (const Handle e : d.handles[d.last()]) {
    if (e != d.absent) { periods.push_back(e); }
  }
  return periods;
}

}  // namespace order
