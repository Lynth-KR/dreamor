// SPDX-License-Identifier: Apache-2.0
// general::TupleHash on the keys a model has - integer grids, entity pointers, strings - which
// std::hash leaves almost as they are: every key gets its own 64-bit hash, and the low bits a
// FlatIndex indexes by spread over the slots as a random hash's would.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "check.hpp"
#include "dreamor/general/tuple_hash.hpp"

namespace {

// The share of keys that land in a slot no other key took, in a table of twice as many slots
// as keys rounded up to a power of two - as FlatIndex sizes it - against a random hash's.
template <typename Key>
bool spreadsLikeRandom(const std::vector<Key>& keys) {
  std::size_t slots = 16;
  while (slots < keys.size() * 2) slots *= 2;
  std::vector<bool> taken(slots);
  std::unordered_set<std::size_t> hashes;
  std::size_t distinct_slots = 0;
  for (const Key& key : keys) {
    const std::size_t hash = dreamor::general::TupleHash{}(key);
    hashes.insert(hash);
    const std::size_t slot = hash & (slots - 1);
    if (!taken[slot]) ++distinct_slots;
    taken[slot] = true;
  }
  const double load   = static_cast<double>(keys.size()) / static_cast<double>(slots);
  const double random = (1 - std::exp(-load)) / load;  // expected share for a random hash
  const double share  = static_cast<double>(distinct_slots) / static_cast<double>(keys.size());
  return hashes.size() == keys.size() && share > 0.98 * random;
}

}  // namespace

int main() try {
  // An integer grid: a combine that leaves the high half unmixed put 1e6 of these on 68k hashes.
  std::vector<std::tuple<std::size_t, std::size_t>> grid;
  for (std::size_t i = 0; i < 1000; ++i)
    for (std::size_t j = 0; j < 1000; ++j) grid.emplace_back(i, j);
  DREAMOR_CHECK(spreadsLikeRandom(grid));

  // Values whose low bits are all zero.
  std::vector<std::tuple<std::uint64_t, std::uint64_t>> aligned;
  for (std::uint64_t i = 0; i < 2000; ++i)
    for (std::uint64_t j = 0; j < 500; ++j) aligned.emplace_back(i << 12U, j << 12U);
  DREAMOR_CHECK(spreadsLikeRandom(aligned));

  // Entity pointers: three columns of objects 32 bytes apart, as an allocator hands them out.
  std::vector<char> arena(std::size_t{3} * 100 * 32);
  std::vector<std::tuple<const char*, const char*, const char*>> pointers;
  for (std::size_t a = 0; a < 100; ++a)
    for (std::size_t b = 0; b < 100; ++b)
      for (std::size_t c = 0; c < 100; ++c)
        pointers.emplace_back(&arena[a * 32], &arena[(100 + b) * 32], &arena[(200 + c) * 32]);
  DREAMOR_CHECK(spreadsLikeRandom(pointers));

  // A name and a period, and a single integer column.
  std::vector<std::tuple<std::string, std::size_t>> named;
  for (std::size_t i = 0; i < 2000; ++i)
    for (std::size_t t = 0; t < 50; ++t) named.emplace_back("I" + std::to_string(i), t);
  DREAMOR_CHECK(spreadsLikeRandom(named));
  std::vector<std::tuple<std::size_t>> single;
  single.reserve(1000000);
  for (std::size_t i = 0; i < 1000000; ++i) single.emplace_back(i);
  DREAMOR_CHECK(spreadsLikeRandom(single));
  return report("tuple_hash");
} catch (...) {
  return ::uncaught();
}
