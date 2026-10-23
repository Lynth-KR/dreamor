// SPDX-License-Identifier: Apache-2.0
// File: general/flat_index.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// Open addressing (linear probing) from a key to an entry number: a row of a set, a group,
// a position. Narrow on purpose - what makes a general hash map hard is not needed here:
//   - insert-only: nothing is ever erased, so there are no tombstones;
//   - load factor <= 1/2, so an empty slot always ends a probe and probes stay short;
//   - TupleHash carries every input bit into the low bits a slot is picked by, so linear
//     probing does not cluster;
//   - slots hold uint32 entry numbers: 4 bytes each, up to 2^32 - 2 entries.
//
// Keys are not stored here. The caller keeps them - a set's rows, a list of group keys - and
// passes key_at(entry) to every call, so a slot is 4 bytes and the caller's storage stays
// the only copy. key_at may hand back a reference or, for a projection, a value.
//
// On a large table every probe is a cache miss, and one key after another they would come one
// after another. Where a run of keys is known up front, hashedBlocks() hashes a block of them,
// prefetches their slots in every index the caller names, and then hands each key's hash to
// the caller's find() and insert(): the block's misses overlap. On 1e7 rows that is about half
// the time.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dreamor/general/tuple_hash.hpp"

namespace dreamor::general {

inline constexpr std::size_t kHashBlock = 32;  // keys hashed, and slots prefetched, at a time

// Calls step(i, hash) for each i in [first, last) in order, hash being TupleHash of key_of(i):
// the keys of a block are hashed, and their slots prefetched in each of indexes, before the
// block's steps run. A step returns false to stop; the result says whether every step ran.
template <typename KeyOf, typename Step, typename... Indexes>
bool hashedBlocks(
    std::size_t first, std::size_t last, const KeyOf& key_of, const Step& step, const Indexes&... indexes) {
  std::array<std::size_t, kHashBlock> hashes{};
  for (std::size_t block = first; block < last; block += kHashBlock) {
    const std::size_t end = std::min(last, block + kHashBlock);
    for (std::size_t i = block; i < end; ++i) {
      hashes[i - block] = TupleHash{}(key_of(i));
      (indexes.prefetch(hashes[i - block]), ...);
    }
    for (std::size_t i = block; i < end; ++i) {
      if (!step(i, hashes[i - block])) {
        return false;
      }
    }
  }
  return true;
}

template <typename Key>
class FlatIndex {
 public:
  static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

  FlatIndex() = default;

  // Room for `count` entries before the first growth.
  explicit FlatIndex(std::size_t count) : slots_(capacityFor(count), kEmpty), mask_{slots_.size() - 1} {}

  // Starts loading the slot a key with this hash - TupleHash's - starts its probe at.
  void prefetch(std::size_t hash) const noexcept {
    if (!slots_.empty()) {
      prefetchForWrite(slots_.data() + (hash & mask_));
    }
  }

  template <typename KeyAt>
  [[nodiscard]] std::size_t find(const Key& key, const KeyAt& key_at) const {
    return find(key, TupleHash{}(key), key_at);
  }
  // find() with the key's hash already known (hashedBlocks()).
  template <typename KeyAt>
  [[nodiscard]] std::size_t find(const Key& key, std::size_t hash, const KeyAt& key_at) const {
    if (slots_.empty()) {
      return kNotFound;
    }
    for (std::size_t slot = hash & mask_;; slot = (slot + 1) & mask_) {
      const std::uint32_t entry = slots_[slot];
      if (entry == kEmpty) {
        return kNotFound;
      }
      if (key_at(entry) == key) {
        return entry;
      }
    }
  }

  // The entry that already holds key, or kNotFound once `entry` holds it. key_at(entry) has
  // to give key by the next insert: growing rehashes every entry through key_at.
  template <typename KeyAt>
  std::size_t insert(const Key& key, std::size_t entry, const KeyAt& key_at) {
    return insert(key, TupleHash{}(key), entry, key_at);
  }
  // insert() with the key's hash already known (hashedBlocks()).
  template <typename KeyAt>
  std::size_t insert(const Key& key, std::size_t hash, std::size_t entry, const KeyAt& key_at) {
    if (entry >= kEmpty) {
      throw std::length_error("dreamor: more than 2^32 - 2 rows to index.");
    }
    if ((size_ + 1) * 2 > slots_.size()) {
      grow(key_at);
    }
    return place(key, hash, entry, key_at);
  }

  // Entries first..last-1, each under key_at(entry), as insert() would put them one by one -
  // up to the first entry whose key is already held: {the entry holding it, the entry repeating
  // it}, or {kNotFound, kNotFound} once all are in. A block of hashes ahead (hashedBlocks()).
  template <typename KeyAt>
  std::pair<std::size_t, std::size_t> insertAll(std::size_t first, std::size_t last, const KeyAt& key_at) {
    if (last > kEmpty) {
      throw std::length_error("dreamor: more than 2^32 - 2 rows to index.");
    }
    while ((size_ + last - first) * 2 > slots_.size()) {
      grow(key_at);  // all at once, so that no prefetch is for a table about to be replaced
    }
    std::pair<std::size_t, std::size_t> repeat{kNotFound, kNotFound};
    hashedBlocks(
        first,
        last,
        key_at,
        [&](std::size_t entry, std::size_t hash) {
          const std::size_t held = place(key_at(entry), hash, entry, key_at);
          if (held != kNotFound) {
            repeat = {held, entry};
          }
          return held == kNotFound;
        },
        *this);
    return repeat;
  }

 private:
  static constexpr std::uint32_t kEmpty     = 0xffffffffU;
  static constexpr std::size_t kMinCapacity = 16;  // A power of two, like every capacity.

  static void prefetchForWrite([[maybe_unused]] const void* address) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(address, 1);
#endif
  }

  // insert() once there is room: the caller has checked the entry and grown the table.
  template <typename KeyAt>
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the hash and the entry, in insert()'s order
  std::size_t place(const Key& key, std::size_t hash, std::size_t entry, const KeyAt& key_at) {
    std::size_t slot = hash & mask_;
    for (; slots_[slot] != kEmpty; slot = (slot + 1) & mask_) {
      if (key_at(slots_[slot]) == key) {
        return slots_[slot];
      }
    }
    slots_[slot] = static_cast<std::uint32_t>(entry);
    ++size_;
    return kNotFound;
  }

  [[nodiscard]] static std::size_t capacityFor(std::size_t count) {
    std::size_t capacity = kMinCapacity;
    while (capacity < count * 2) {
      capacity *= 2;
    }
    return capacity;
  }

  template <typename KeyAt>
  void grow(const KeyAt& key_at) {
    const std::vector<std::uint32_t> old = std::move(slots_);
    slots_.assign(old.empty() ? kMinCapacity : old.size() * 2, kEmpty);
    mask_ = slots_.size() - 1;
    for (const std::uint32_t entry : old) {
      if (entry == kEmpty) {
        continue;
      }
      std::size_t slot = TupleHash{}(key_at(entry)) & mask_;
      while (slots_[slot] != kEmpty) {
        slot = (slot + 1) & mask_;
      }
      slots_[slot] = entry;
    }
  }

  std::vector<std::uint32_t> slots_;
  std::size_t mask_ = 0;
  std::size_t size_ = 0;
};

}  // namespace dreamor::general
