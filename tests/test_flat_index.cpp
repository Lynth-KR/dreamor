// SPDX-License-Identifier: Apache-2.0
// general::FlatIndex on its own: growth through several doublings, repeats, absent keys, and
// a projected key that is built on every call rather than stored.
#include <cstddef>
#include <tuple>
#include <utility>
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

  // insertAll: a block at a time, the same as insert one by one - into an empty index that grows,
  // after entries already in, and up to the first repeat, wherever it falls in a block.
  Index batch;
  DREAMOR_CHECK(batch.insertAll(0, keys.size(), at) == std::pair{Index::kNotFound, Index::kNotFound});
  bool batch_found = true;
  for (std::size_t i = 0; i < keys.size(); ++i) batch_found = batch_found && batch.find(keys[i], at) == i;
  DREAMOR_CHECK(batch_found && batch.find(Key{50, 0}, at) == Index::kNotFound);
  std::vector<Key> more = keys;
  more.reserve(keys.size() + 100);
  for (std::size_t i = 0; i < 100; ++i) more.emplace_back(i, 100);
  const auto at_more = [&more](std::size_t entry) -> const Key& { return more[entry]; };
  DREAMOR_CHECK(batch.insertAll(keys.size(), more.size(), at_more) == std::pair{Index::kNotFound, Index::kNotFound});
  DREAMOR_CHECK(batch.find(Key{99, 100}, at_more) == more.size() - 1 && batch.find(Key{7, 3}, at_more) == 157);
  for (const std::size_t repeat_at : {1U, 31U, 32U, 33U, 70U}) {
    std::vector<Key> with_repeat;
    with_repeat.reserve(100);
    for (std::size_t i = 0; i < 100; ++i) with_repeat.emplace_back(i, 7);
    with_repeat[repeat_at] = with_repeat[repeat_at / 2];
    const auto at_repeat   = [&with_repeat](std::size_t entry) -> const Key& { return with_repeat[entry]; };
    Index stops{with_repeat.size()};
    DREAMOR_CHECK(stops.insertAll(0, with_repeat.size(), at_repeat) == std::pair{repeat_at / 2, repeat_at});
    DREAMOR_CHECK(stops.find(with_repeat[repeat_at - 1], at_repeat) == repeat_at - 1);
  }
  return report("flat_index");
} catch (...) {
  return ::uncaught();
}
