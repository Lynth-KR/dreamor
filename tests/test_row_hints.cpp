// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A lookup by columns against hints that point anywhere. The hints - the row sumOf published, the
// rows the thread's last lookups found - are addresses, checked against the key and never trusted:
// a row of a set gone, of any key, can leave one at any byte of a set that now has its memory. So,
// whatever the allocator does:
//   - a hint at every byte of every row and just outside the rows, in each of the four slots
//     (walking or not) and as the published row, over keys of every size and alignment columns
//     make - 4 to 96 bytes, padded, pointers, strings;
//   - sets of several keys made, looked up by columns in turn and gone, three times over;
//   - lookups on several threads at once, the row indexes not built yet.
// Each lookup by columns gives what the row's address gives, and a key in no row is in none.
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"

namespace {

struct Ent {
  std::uint32_t id;
};
std::uint32_t keyName(const Ent* e) {
  return e->id;
}

constexpr std::size_t kAbsent = 8;  // a column's value that no row has
const std::array<Ent, kAbsent + 1> kEnts{{{0}, {1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}}};

// A column's i-th value.
template <typename T>
T valueOf(std::size_t i) {
  if constexpr (std::is_same_v<T, const Ent*>) {
    return &kEnts.at(i);
  } else if constexpr (std::is_same_v<T, std::string>) {
    return std::string(1, static_cast<char>('a' + i));
  } else {
    return static_cast<T>(i);
  }
}

// A column's first n values, as makeSet takes them: an entity column, the entities themselves.
template <typename C>
auto valuesOf(std::size_t n, std::size_t shift) {
  using T = C::Value;
  if constexpr (std::is_same_v<T, const Ent*>) {
    return std::span<const Ent>(kEnts).subspan(shift, n);
  } else {
    std::vector<T> values;
    values.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      values.push_back(valueOf<T>(i + shift));
    }
    return values;
  }
}

// Every combination of each column's n values from shift on. makeSet takes its containers as
// named variables only, so they are held here until the set is collected.
template <typename... Cols>
dreamor::SetOf<Cols...> gridOf(std::size_t n, std::size_t shift) {
  auto values = std::make_tuple(valuesOf<Cols>(n, shift)...);
  return std::apply([](auto&... v) { return dreamor::makeSet<Cols...>(v...).collect(); }, values);
}

// A few rows whatever the columns: 8 for one column, 2 a column from four on.
template <typename... Cols>
dreamor::SetOf<Cols...> rowsOf(std::size_t shift) {
  constexpr std::array<std::size_t, 4> kValues{8, 4, 3, 2};
  return gridOf<Cols...>(kValues.at(std::min<std::size_t>(sizeof...(Cols), kValues.size()) - 1), shift);
}

// A row's columns copied out of the set: a key at an address of its own, as a lookup by columns
// is mostly given. A lookup handed the set's own columns takes their address for the row.
template <typename Row>
Row apart(const Row& row) {
  return row;
}

template <typename Table, typename Row>
const IloNumVarI* byColumns(const Table& x, const Row& key) {
  return std::apply([&x](const auto&... c) { return x(c...).getImpl(); }, key);
}
template <typename Table, typename Row>
bool hasColumns(const Table& x, const Row& key) {
  return std::apply([&x](const auto&... c) { return x.contains(c...); }, key);
}

// Every byte of the rows, the byte before them and the one past them, as a hint in each place a
// hint can be; with it, the lookup of the row the byte is in, the row after, the first row and a
// key in no row. Small values, so that bytes across two rows read as a row's columns.
template <typename... Cols>
bool sweep(IloEnv env) {
  using Row       = std::tuple<Cols...>;
  const auto rows = rowsOf<Cols...>(0);
  const dreamor::concert::VarTable<Cols...> x{env, rows, 0, 1};
  const Row absent{Cols{valueOf<typename Cols::Value>(kAbsent)}...};
  const auto first        = std::bit_cast<std::uintptr_t>(&*rows.begin());
  const std::size_t bytes = rows.size() * sizeof(Row);

  auto& hints      = dreamor::internal::lastRows();
  bool agree       = true;
  const auto rowAt = [&rows](std::size_t at) -> const Row& {
    return *std::next(rows.begin(), static_cast<std::ptrdiff_t>(at % rows.size()));
  };
  for (std::size_t offset = 0; offset < bytes + 2; ++offset) {
    const auto* const hint = std::bit_cast<const void*>(first + offset - 1);
    const std::size_t at   = offset == 0 ? 0 : (offset - 1) / sizeof(Row);
    for (std::size_t place = 0; place < (2 * hints.size()) + 1; ++place) {
      const auto plant = [&] {
        hints = {};
        if (place < 2 * hints.size()) { hints[place / 2] = {.row = hint, .walking = place % 2 == 1}; }
      };
      const auto lookUp = [&] {
        for (const std::size_t to : {at, at + 1, std::size_t{0}}) {
          plant();
          const Row& row = rowAt(to);
          const Row key  = apart(row);
          agree          = agree && byColumns(x, key) == x(row).getImpl();
          plant();
          agree = agree && hasColumns(x, key);
        }
        plant();
        agree = agree && !hasColumns(x, absent);
      };
      if (place == 2 * hints.size()) {
        const dreamor::internal::CurrentRowScope published{hint};
        lookUp();
      } else {
        lookUp();
      }
    }
  }
  hints = {};
  return agree;
}

// Sets of four keys - 16, 24, 12 and 40 bytes - and a table on each, looked up by columns in turn,
// every row; then gone, and made again, in another order and on other values: the hints each key
// left lie where the others' sets may now be.
using S0 = dreamor::Col<std::size_t, "s0">;
using S1 = dreamor::Col<std::size_t, "s1">;
using S2 = dreamor::Col<std::size_t, "s2">;
using S3 = dreamor::Col<std::size_t, "s3">;
using S4 = dreamor::Col<std::size_t, "s4">;
using S5 = dreamor::Col<std::size_t, "s5">;
using S6 = dreamor::Col<std::size_t, "s6">;
using U0 = dreamor::Col<std::uint32_t, "u0">;
using U1 = dreamor::Col<std::uint32_t, "u1">;
using U2 = dreamor::Col<std::uint32_t, "u2">;
using P0 = dreamor::Col<const Ent*, "p0">;
using P1 = dreamor::Col<const Ent*, "p1">;
using P2 = dreamor::Col<const Ent*, "p2">;
using P3 = dreamor::Col<const Ent*, "p3">;
using P4 = dreamor::Col<const Ent*, "p4">;
using P5 = dreamor::Col<const Ent*, "p5">;
using P6 = dreamor::Col<const Ent*, "p6">;
using P7 = dreamor::Col<const Ent*, "p7">;
using T0 = dreamor::Col<std::string, "t0">;
using T1 = dreamor::Col<std::string, "t1">;
using T2 = dreamor::Col<std::string, "t2">;

template <typename... Cols>
struct Family {
  dreamor::SetOf<Cols...> rows;
  dreamor::concert::VarTable<Cols...> x;
  Family(IloEnv env, std::size_t shift) : rows{rowsOf<Cols...>(shift)}, x{env, rows, 0, 1} {}
  [[nodiscard]] bool agrees(std::size_t at) const {
    const auto& row               = *std::next(rows.begin(), static_cast<std::ptrdiff_t>(at % rows.size()));
    const std::tuple<Cols...> key = row;
    return byColumns(x, key) == x(row).getImpl();
  }
};

bool inTurns(IloEnv env) {
  bool agree = true;
  for (std::size_t round = 0; round < 3; ++round) {
    std::unique_ptr<Family<S0, S1>> a;
    std::unique_ptr<Family<S0, S1, S2>> b;
    std::unique_ptr<Family<U0, U1, U2>> c;
    std::unique_ptr<Family<S0, S1, S2, S3, S4>> d;
    for (std::size_t made = 0; made < 4; ++made) {
      switch ((made + round) % 4) {
        case 0: a = std::make_unique<Family<S0, S1>>(env, round); break;
        case 1: b = std::make_unique<Family<S0, S1, S2>>(env, round); break;
        case 2: c = std::make_unique<Family<U0, U1, U2>>(env, round); break;
        default: d = std::make_unique<Family<S0, S1, S2, S3, S4>>(env, round); break;
      }
    }
    for (std::size_t at = 0; at < 32; ++at) {
      agree = agree && a->agrees(at) && b->agrees(at) && c->agrees(at) && d->agrees(at);
    }
  }
  return agree;
}

// Tables on two shared sets and one of each thread's own, none looked up yet; on each of 8
// threads, a walk over every set, then its keys in a scattered order of the thread's own, then
// keys in no row. Whichever lookup needs a set's hash first builds it; the others wait for it.
bool onThreads(IloEnv env) {
  constexpr std::size_t kThreads = 8;
  const auto wide                = gridOf<S0, S1>(16, 0);
  const auto deep                = gridOf<S0, S1, S2>(6, 0);
  const dreamor::concert::VarTable<S0, S1> xWide{env, wide, 0, 1};
  const dreamor::concert::VarTable<S0, S1, S2> xDeep{env, deep, 0, 1};
  std::vector<dreamor::SetOf<U0, U1, U2>> own;
  own.reserve(kThreads);
  for (std::size_t t = 0; t < kThreads; ++t) {
    own.push_back(rowsOf<U0, U1, U2>(t));
  }
  std::vector<std::unique_ptr<dreamor::concert::VarTable<U0, U1, U2>>> xOwn;
  xOwn.reserve(kThreads);
  for (const auto& rows : own) {
    xOwn.push_back(std::make_unique<dreamor::concert::VarTable<U0, U1, U2>>(env, rows, 0, 1));
  }

  // A set walked, then its rows in a scattered order: a stride coprime with its size.
  const auto finds = [](const auto& x, const auto& rows, std::size_t stride) {
    bool all = true;
    for (std::size_t pass = 0; pass < 2; ++pass) {
      for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::size_t at = pass == 0 ? i : (i * stride) % rows.size();
        const auto& row      = *std::next(rows.begin(), static_cast<std::ptrdiff_t>(at));
        const auto key       = apart(row);
        all                  = all && byColumns(x, key) == x(row).getImpl();
      }
    }
    return all;
  };
  // Coprime with 256, 216 (2^3 3^3) and 27: no factor 2 or 3.
  constexpr std::array<std::size_t, kThreads> kStrides{1, 5, 7, 11, 13, 17, 19, 23};
  std::array<bool, kThreads> agree{};
  {
    std::vector<std::jthread> threads;
    threads.reserve(kThreads);
    for (std::size_t t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t] {
        const std::size_t stride = kStrides.at(t);
        agree.at(t) = finds(xWide, wide, stride) && finds(xDeep, deep, stride) && finds(*xOwn[t], own[t], stride) &&
                      !xWide.contains(S0{16}, S1{0}) && !xDeep.contains(S0{0}, S1{0}, S2{6});
      });
    }
  }
  bool all = true;
  for (const bool a : agree) {
    all = all && a;
  }
  return all;
}

}  // namespace

int main() try {
  const IloEnv env;
  DREAMOR_CHECK(sweep<U0>(env));                              // 4 bytes
  DREAMOR_CHECK(sweep<U0, U1, U2>(env));                      // 12
  DREAMOR_CHECK(sweep<U0, S0>(env));                          // 16, 4 of them padding
  DREAMOR_CHECK(sweep<S0, S1, S2>(env));                      // 24
  DREAMOR_CHECK(sweep<P0, P1, P2, P3>(env));                  // 32
  DREAMOR_CHECK(sweep<T0, S0>(env));                          // 40 with libstdc++'s string
  DREAMOR_CHECK(sweep<S0, S1, S2, S3, S4>(env));              // 40
  DREAMOR_CHECK(sweep<S0, S1, S2, S3, S4, S5>(env));          // 48
  DREAMOR_CHECK(sweep<S0, S1, S2, S3, S4, S5, S6>(env));      // 56
  DREAMOR_CHECK(sweep<P0, P1, P2, P3, P4, P5, P6, P7>(env));  // 64
  DREAMOR_CHECK(sweep<T0, T1, T2>(env));                      // 96
  DREAMOR_CHECK(inTurns(env));
  DREAMOR_CHECK(onThreads(env));
  return report("row_hints");
} catch (...) { return ::uncaught(); }
