// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// general::TupleHash on the keys a model has - integer grids, entity pointers, strings - which
// std::hash leaves almost as they are: every key gets its own 64-bit hash, and the low bits a
// FlatIndex indexes by spread over the slots as a random hash's would.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "check.hpp"
#include "dreamor/general/tuple_hash.hpp"
#include "dreamor/index_set/column.hpp"

namespace {

// The share of keys that land in a slot no other key took, in a table of twice as many slots
// as keys rounded up to a power of two - as FlatIndex sizes it - against a random hash's.
template <typename Key>
bool spreadsLikeRandom(const std::vector<Key>& keys) {
  std::size_t slots = 16;
  while (slots < keys.size() * 2) {
    slots *= 2;
  }
  std::vector<bool> taken(slots);
  std::unordered_set<std::size_t> hashes;
  std::size_t distinct_slots = 0;
  for (const Key& key : keys) {
    const std::size_t hash = dreamor::general::TupleHash{}(key);
    hashes.insert(hash);
    const std::size_t slot = hash & (slots - 1);
    if (!taken[slot]) { ++distinct_slots; }
    taken[slot] = true;
  }
  const double load   = static_cast<double>(keys.size()) / static_cast<double>(slots);
  const double random = (1 - std::exp(-load)) / load;  // expected share for a random hash
  const double share  = static_cast<double>(distinct_slots) / static_cast<double>(keys.size());
  // Distinct keys, distinct hashes: every one where std::size_t is 64 bits; where it is 32 (wasm32),
  // about the n^2 / 2^33 pairs a random hash collides on (the birthday bound), and no more than twice that.
  const auto n          = static_cast<double>(keys.size());
  const double expected = n * n / std::ldexp(2.0, std::numeric_limits<std::size_t>::digits);
  const auto collisions = static_cast<double>(keys.size() - hashes.size());
  return collisions <= 2 * expected && share > 0.98 * random;
}

enum class Shift : std::uint8_t { kNight = 3 };
using cA = dreamor::Col<std::int64_t, "a">;
using cB = dreamor::Col<std::uint16_t, "b">;

// What every platform must hash these keys to: the 64-bit state, of which a 32-bit std::size_t holds
// the low half. Integers, enums and columns of them only - std::hash (a string) is each library's own,
// and a pointer each run's.
template <typename... Ts>
bool hashesTo(const std::tuple<Ts...>& key, std::uint64_t expected) {
  return dreamor::general::TupleHash{}(key) == static_cast<std::size_t>(expected);
}

}  // namespace

int main() try {
  // The compiler's wide multiply (unsigned __int128, __umulh) and the one in 32-bit halves agree, bit
  // for bit: on the edges of the halves, and on random pairs.
  {
    using dreamor::general::foldedMultiply;
    using dreamor::general::foldedMultiplyPortable;
    constexpr std::array<std::uint64_t, 6> kEdges{
        0,
        1,
        0xffffffffULL,
        0x100000000ULL,
        ~std::uint64_t{0},
        0x9e3779b97f4a7c15ULL,
    };
    bool agree = true;
    for (const std::uint64_t a : kEdges) {
      for (const std::uint64_t b : kEdges) {
        agree = agree && foldedMultiply(a, b) == foldedMultiplyPortable(a, b);
      }
    }
    // NOLINTNEXTLINE(bugprone-random-generator-seed): fixed on purpose, every run checks the same pairs
    std::mt19937_64 random{20261104};
    for (std::size_t i = 0; i < 100000; ++i) {
      const std::uint64_t a = random();
      const std::uint64_t b = random();
      agree                 = agree && foldedMultiply(a, b) == foldedMultiplyPortable(a, b);
    }
    DREAMOR_CHECK(agree);
  }

  // The same hash on every platform.
  DREAMOR_CHECK(hashesTo(std::tuple<std::uint64_t>{0}, 0x86b07c27d10d4567ULL));
  DREAMOR_CHECK(hashesTo(std::tuple<std::int64_t, std::uint32_t>{-1, 7}, 0x8fc42df787a7f58dULL));
  DREAMOR_CHECK(hashesTo(
      std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>{std::uint64_t{1} << 40U, 2, 0xdeadbeefcafebabeULL},
      0xc5edb600197a6459ULL));
  DREAMOR_CHECK(hashesTo(std::tuple<Shift, std::int32_t>{Shift::kNight, -5}, 0x30a7e0c6fc4f829aULL));
  // A column hashes as the value it wraps, the 64 bits of it.
  DREAMOR_CHECK(hashesTo(std::tuple<cA, cB>{cA{-5000000000LL}, cB{9}}, 0xec5bee906a4be5c6ULL));
  DREAMOR_CHECK(hashesTo(std::tuple<std::int64_t, std::uint16_t>{-5000000000LL, 9}, 0xec5bee906a4be5c6ULL));

  // An integer grid: a combine that leaves the high half unmixed put 1e6 of these on 68k hashes.
  std::vector<std::tuple<std::size_t, std::size_t>> grid;
  for (std::size_t i = 0; i < 1000; ++i) {
    for (std::size_t j = 0; j < 1000; ++j) {
      grid.emplace_back(i, j);
    }
  }
  DREAMOR_CHECK(spreadsLikeRandom(grid));

  // Values whose low bits are all zero.
  std::vector<std::tuple<std::uint64_t, std::uint64_t>> aligned;
  for (std::uint64_t i = 0; i < 2000; ++i) {
    for (std::uint64_t j = 0; j < 500; ++j) {
      aligned.emplace_back(i << 12U, j << 12U);
    }
  }
  DREAMOR_CHECK(spreadsLikeRandom(aligned));

  // Entity pointers: three columns of objects 32 bytes apart, as an allocator hands them out.
  std::vector<char> arena(std::size_t{3} * 100 * 32);
  std::vector<std::tuple<const char*, const char*, const char*>> pointers;
  for (std::size_t a = 0; a < 100; ++a) {
    for (std::size_t b = 0; b < 100; ++b) {
      for (std::size_t c = 0; c < 100; ++c) {
        pointers.emplace_back(&arena[a * 32], &arena[(100 + b) * 32], &arena[(200 + c) * 32]);
      }
    }
  }
  DREAMOR_CHECK(spreadsLikeRandom(pointers));

  // A name and a period, and a single integer column.
  std::vector<std::tuple<std::string, std::size_t>> named;
  for (std::size_t i = 0; i < 2000; ++i) {
    for (std::size_t t = 0; t < 50; ++t) {
      named.emplace_back("I" + std::to_string(i), t);
    }
  }
  DREAMOR_CHECK(spreadsLikeRandom(named));
  std::vector<std::tuple<std::size_t>> single;
  single.reserve(1000000);
  for (std::size_t i = 0; i < 1000000; ++i) {
    single.emplace_back(i);
  }
  DREAMOR_CHECK(spreadsLikeRandom(single));
  return report("tuple_hash");
} catch (...) { return ::uncaught(); }
