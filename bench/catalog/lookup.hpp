// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/lookup.hpp - the lookup family: a variable found by the columns of its key.
//
// The phases are the same in every arm:
// - build: the rows, x on them, the (k1, period) pairs that the rows have, and y on those pairs.
// - index: what the lookups below go through. For dreamor, these are the row indexes of x and y, which the first
//   lookup that needs them builds, and which this phase forces. For a hand-written arm, it is the arm's map or array.
// - neighbor: x of every row at the period before, if that row exists: contains(), then the lookup.
// - project: y of every row at its (k1, period), a family on fewer columns.
// - hit: x at keys drawn at random from the rows.
// - miss: contains() of the same keys with the period in no row.
// - half: contains(), then x if the key is there, over keys of which half are rows.
//
// At k = 1 a row is its k1 alone. There is no period, so there is no y, no neighbor and no project. A miss is a k1 in
// no row, a different one each, as data.hpp describes.
//
// dreamor is compared with hand-written arms that pack the ids into one integer and keep the rows in a vector:
// - identity finds a row through a std::unordered_map with libstdc++'s identity hash.
// - mixed finds it through a std::unordered_map with a mixed hash.
// - array finds it through a direct array over every combination of ids. A hand-coder takes it when the ids are dense
//   and their product fits in memory.
// Packing needs the product of the id counts to fit in 64 bits. Where it does not, those arms do not run. map is the
// usual hand-written code, and it runs everywhere: one std::map from a row's entities to its variable, filled as the
// rows are generated, as examples/production_planning_plain.hpp does. map has no index of its own, so its index phase
// is empty.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "shared.hpp"

namespace lookup {

using catalog::MixedHash;
using catalog::Packer;
using catalog::serialsOf;
using catalog::ubOf;

inline const std::vector<std::string> kPhases = {"build", "index", "neighbor", "project", "hit", "miss", "half"};

}  // namespace lookup
