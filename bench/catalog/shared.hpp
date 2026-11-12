// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/shared.hpp - what the families share: tags, packed keys, and how an expression is checked.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ilcplex/ilocplex.h>

#include "data.hpp"

namespace catalog {

// Returns the serials of the first k entities of a row, from which tagOf makes a tag.
inline std::array<std::uint32_t, kMaxColumns> serialsOf(const std::array<const Ent*, kMaxColumns>& row, std::size_t k) {
  std::array<std::uint32_t, kMaxColumns> s{};
  for (std::size_t j = 0; j < k; ++j) {
    s[j] = row[j]->serial;
  }
  return s;
}

inline std::uint64_t ubOf(const IloNumVar& v) {
  return static_cast<std::uint64_t>(v.getUB());
}

// A hash that mixes every bit of the key into the bucket, so that the order of the ids does not show in the order of
// the buckets.
struct MixedHash {
  std::size_t operator()(std::uint64_t x) const noexcept { return mix(x); }
};

// Packs the ids of a row into one integer, with column 0 the most significant, as a hand-coder writes it. size[j] is
// the number of ids that column j can hold.
struct Packer {
  std::array<std::uint64_t, kMaxColumns> size{};
  std::size_t k = 0;

  [[nodiscard]] std::uint64_t operator()(const std::array<std::uint32_t, kMaxColumns>& id) const {
    std::uint64_t key = 0;
    for (std::size_t j = 0; j < k; ++j) {
      key = key * size[j] + id[j];
    }
    return key;
  }

  [[nodiscard]] std::array<std::uint32_t, kMaxColumns> unpack(std::uint64_t key) const {
    std::array<std::uint32_t, kMaxColumns> id{};
    for (std::size_t j = k; j-- > 0;) {
      id[j] = static_cast<std::uint32_t>(key % size[j]);
      key /= size[j];
    }
    return id;
  }

  [[nodiscard]] long double span() const {
    long double p = 1;
    for (std::size_t j = 0; j < k; ++j) {
      p *= static_cast<long double>(size[j]);
    }
    return p;
  }
};

// Reads the terms of a finished expression, as a solver does when the expression is added, and ends the expression.
// Every arm does this with each expression that it makes. Returns the sum of each term's upper bound times its
// coefficient, multiplied by a hash of the group's key, or 0 if the expression has no term. A phase's check is the sum
// over its groups, so the check does not depend on the order of the groups.
inline std::uint64_t consume(IloExpr& e, std::uint64_t key) {
  std::uint64_t sum = 0;
  const IloExprI& x = *e.getImpl();
  for (std::size_t i = 0; i < x.vars.size(); ++i) {
    sum += static_cast<std::uint64_t>(x.vars[i]->ub) * static_cast<std::uint64_t>(x.coefs[i]);
  }
  const bool empty = x.vars.empty();
  e.end();  // x is not valid after this line, because end() frees it.
  return empty ? 0 : sum * (mix(key * 0x9e3779b97f4a7c15ULL) | 1);
}

// Returns a hash of the first count serials: the key of a group, or a whole row.
template <typename Serials>
inline std::uint64_t keyOf(const Serials& serials, std::size_t count) {
  std::uint64_t h = 0;
  for (std::size_t j = 0; j < count; ++j) {
    h = mix(h ^ (std::uint64_t{serials[j]} << (8 * j)));
  }
  return h;
}

}  // namespace catalog
