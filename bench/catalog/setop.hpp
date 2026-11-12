// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/setop.hpp - the set-operation family: projection, union, intersection, difference and membership.
//
// A is the rows of the set that a predicate on (k1, period) keeps, about two in three. B is generated apart, from the
// same lists with every period moved one on. A predicate on its last two columns keeps some of its rows, and so does
// the predicate of a sparse cell. So B overlaps A in part. Neither A nor B is measured. The phases are the same in
// every arm:
// - build: the rows of the set.
// - select: the rows projected on every column but k1, each projection once.
// - unite: A, then the rows of B that A does not have.
// - intersect: the rows of A that B has.
// - difference: the rows of A that B does not have.
// - member: the rows of the set that B has, through the membership of B.
// The check of a result sums the hashes of its rows and counts the rows, so the check does not depend on their order.
// dreamor keeps the order of first appearance, and a merge gives the order of the keys.
//
// dreamor is compared with three hand-written arms over packed ids and the usual code:
// - identity and mixed keep a std::unordered_set, with the identity hash or a mixed one.
// - merge sorts the keys and uses <algorithm>: std::sort, std::unique, std::set_union, std::set_intersection,
//   std::set_difference and std::binary_search.
// - set, the usual code, keeps a std::set of the rows' entities.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "shared.hpp"

namespace setop {

using catalog::keyOf;
using catalog::MixedHash;
using catalog::Packer;

constexpr std::uint64_t kCountWeight = 0x9e3779b97f4a7c15ULL;

inline bool inA(std::uint32_t k1, std::uint32_t period) {
  return (k1 + period) % 3 != 0;
}

inline bool inB(std::uint32_t before_last, std::uint32_t period) {
  return (before_last + period) % 2 == 0;
}

// Returns the periods of each k1 moved one on, from which B is generated.
template <typename Arm>
inline std::vector<std::vector<typename Arm::Handle>> movedPeriods(const DataOf<Arm>& d) {
  using Handle           = typename Arm::Handle;
  const std::size_t pool = d.ents[d.last()].size() - 1;  // The absent period is in no list, so it is not counted.
  std::vector<std::vector<Handle>> moved;
  moved.reserve(d.lists[d.last()].size());
  for (const auto& l : d.lists[d.last()]) {
    std::vector<Handle> m;
    m.reserve(l.size());
    for (const Handle t : l) {
      m.push_back(d.handles[d.last()][(t->serial + 1) % pool]);
    }
    moved.push_back(std::move(m));
  }
  return moved;
}

}  // namespace setop
