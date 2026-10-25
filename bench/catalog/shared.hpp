// SPDX-License-Identifier: Apache-2.0
// catalog/shared.hpp - what the families share: tags, packed keys, and how an expression is checked
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "data.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

namespace catalog {

// The serials of a row of entities, for tags.
inline std::array<std::uint32_t, kMaxColumns> serialsOf(const std::array<const Ent*, kMaxColumns>& row, std::size_t k) {
  std::array<std::uint32_t, kMaxColumns> s{};
  for (std::size_t j = 0; j < k; ++j) s[j] = row[j]->serial;
  return s;
}

inline std::uint64_t ubOf(const IloNumVar& v) {
  return static_cast<std::uint64_t>(v.getUB());
}

// MixedHash: every bit of the key in the bucket, so the order of the ids does not show in the
// order of the buckets.
struct MixedHash {
  std::size_t operator()(std::uint64_t x) const noexcept { return mix(x); }
};

// Packs ids into one integer, column 0 most significant: what a hand-coder writes.
struct Packer {
  std::array<std::uint64_t, kMaxColumns> size{};
  std::size_t k = 0;
  [[nodiscard]] std::uint64_t operator()(const std::array<std::uint32_t, kMaxColumns>& id) const {
    std::uint64_t key = 0;
    for (std::size_t j = 0; j < k; ++j) key = key * size[j] + id[j];
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
    for (std::size_t j = 0; j < k; ++j) p *= static_cast<long double>(size[j]);
    return p;
  }
};

// What every arm does with a finished expression: read its terms (as a solver would when the
// expression is added), fold them under a hash of the group's key, and end it. The sum over a
// phase's groups is the phase's check; it does not depend on the order of the groups.
inline std::uint64_t consume(IloExpr& e, std::uint64_t key) {
  std::uint64_t sum = 0;
  const IloExprI& x = *e.getImpl();
  for (std::size_t i = 0; i < x.vars.size(); ++i) {
    sum += static_cast<std::uint64_t>(x.vars[i]->ub) * static_cast<std::uint64_t>(x.coefs[i]);
  }
  const bool empty = x.vars.empty();
  e.end();  // frees x
  return empty ? 0 : sum * (mix(key * 0x9e3779b97f4a7c15ULL) | 1);
}

// A group's key: the serials of its columns, folded.
template <typename Serials>
inline std::uint64_t keyOf(const Serials& serials, std::size_t count) {
  std::uint64_t h = 0;
  for (std::size_t j = 0; j < count; ++j) h = mix(h ^ (std::uint64_t{serials[j]} << (8 * j)));
  return h;
}

// The set every dreamor arm builds, for k = sizeof...(J) columns: the k1s, then column j's list of
// each k1, kept by the predicate when the cell is sparse; x on it, tagged by its rows' serials.
template <typename Seq>
struct Gen;
template <std::size_t... J>
struct Gen<std::index_sequence<J...>> {
  static constexpr std::size_t kK    = sizeof...(J);
  static constexpr std::size_t kLast = kK - 1;
  using Set                          = dreamor::SetOf<KCol<J>...>;
  using Table                        = dreamor::concert::VarTable<KCol<J>...>;

  // Callables take the columns they read, by type: no generic lambdas.
  struct Keep {
    const Data* d;
    bool operator()(KCol<J>... c) const { return d->keep(std::array<std::uint32_t, kMaxColumns>{c->serial...}); }
  };
  struct Tag {
    double operator()(KCol<J>... c) const { return tagOf(std::array<std::uint32_t, kMaxColumns>{c->serial...}, kK, 0); }
  };
  template <std::size_t I>
  struct ListOf {
    const Data* d;
    std::span<const Ent* const> operator()(KCol<0> k1) const { return d->lists[I][k1->serial]; }
  };

  static Set make(const Data& d) {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      return d.sparse ? dreamor::makeSet<KCol<J>...>(d.k1s, ListOf<I + 1>{&d}...).where(Keep{&d}).collect()
                      : dreamor::makeSet<KCol<J>...>(d.k1s, ListOf<I + 1>{&d}...).collect();
    }(std::make_index_sequence<kK - 1>{});
  }
};

}  // namespace catalog
