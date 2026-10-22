// SPDX-License-Identifier: Apache-2.0
// general::FlatIndex on its own: growth through several doublings, repeats, absent keys, and
// a projected key that is built on every call rather than stored.
#include <cstddef>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "dreamor/general/flat_index.hpp"

using Key   = std::tuple<std::size_t, std::size_t>;
using Index = dreamor::general::FlatIndex<Key>;

int main() try {
  // Grown from nothing: 16 slots, then doubling past 1000 entries.
  std::vector<Key> keys;
  Index grown;
  const auto at = [&keys](std::size_t entry) -> const Key& { return keys[entry]; };
  DREAMOR_CHECK(grown.find(Key{0, 0}, at) == Index::kNotFound);
  for (std::size_t i = 0; i < 1000; ++i) {
    const Key key{i % 50, i / 50};
    DREAMOR_CHECK(grown.insert(key, keys.size(), at) == Index::kNotFound);
    keys.push_back(key);
  }
  bool all_found = true;
  for (std::size_t i = 0; i < keys.size(); ++i) all_found = all_found && grown.find(keys[i], at) == i;
  DREAMOR_CHECK(all_found);
  DREAMOR_CHECK(grown.find(Key{50, 0}, at) == Index::kNotFound && grown.find(Key{0, 20}, at) == Index::kNotFound);

  // A repeat hands back the entry that holds the key and is not placed.
  DREAMOR_CHECK(grown.insert(Key{7, 3}, keys.size(), at) == 157);

  // Sized up front, with the key projected out of a wider row.
  const std::vector<std::tuple<std::size_t, std::size_t, char>> rows{{1, 2, 'a'}, {3, 4, 'b'}, {1, 2, 'c'}};
  const auto projected = [&rows](std::size_t row) { return Key{std::get<0>(rows[row]), std::get<1>(rows[row])}; };
  Index sized{rows.size()};
  DREAMOR_CHECK(sized.insert(projected(0), 0, projected) == Index::kNotFound);
  DREAMOR_CHECK(sized.insert(projected(1), 1, projected) == Index::kNotFound);
  DREAMOR_CHECK(sized.insert(projected(2), 2, projected) == 0);
  DREAMOR_CHECK(sized.find(Key{3, 4}, projected) == 1 && sized.find(Key{2, 1}, projected) == Index::kNotFound);
  return report("flat_index");
} catch (...) {
  return ::uncaught();
}
