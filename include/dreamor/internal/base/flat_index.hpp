// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A hash table with open addressing and linear probing, from a key to an entry number, such as a set's row, a group
// or a position. It does only what its callers need:
// - Entries are inserted and never removed, so the table needs no tombstones.
// - The load factor is at most 1/2, so an empty slot ends every probe.
// - TupleHash spreads every bit of a key into the low bits of the hash, so linear probing does not cluster.
// - A slot has 32 bits, so a table holds up to 2^32 - 2 entries.
//
// The table does not store keys. The caller keeps them and passes key_at(entry), which returns a reference or, for a
// projection, a value. The bits of a slot above its entry number hold the top bits of the key's hash, the tag. A
// probe reads a key only if the tags agree, because reading a key of a large table is a cache miss. Two kinds of
// table have no tags: a table under 2^16 slots, whose keys are in the cache, and a table of 2^32 slots or more,
// which has no bits to spare. Their tag is 0, and their mask keeps every bit.
//
// hashedBlocks() hashes a run of keys that are known in advance and prefetches their slots before it hands them on.
// On a table larger than the cache, the cache misses then overlap.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dreamor/internal/base/inline.hpp"
#include "dreamor/internal/base/tuple_hash.hpp"

/// @section Exceptions
///
/// This section lists the exceptions that the entries of this reference throw, by type. Under each type are the
/// entries that throw it.
///
/// One exception is thrown by so many entries that it is described here once. dreamor numbers the rows of a set and
/// the entities of an `EntityVector` with 32-bit integers when it indexes them. An index holds up to 2^32 - 2 rows or
/// entities. Building an index over 2^32 or more rows or entities throws `std::length_error`. dreamor builds such an
/// index wherever it finds a row, a group or an entity by its key.
///
/// A lookup that finds nothing does not throw where nothing is a result: `Grouped::slice` of a key with no row returns
/// an empty `RowSlice`, whose sum is 0; `Ordered`'s `next`, `prev`, `nextCyclic` and `prevCyclic` return a null
/// pointer; and `Ordered::positionOf` returns an empty `std::optional`.

namespace dreamor::internal::inline base {

inline constexpr std::size_t kHashBlock = 32;  // The number of keys hashed, and slots prefetched, at a time.

// Calls on_block(hashes, block, count) for each block of [first, last) in order. hashes[k] is the TupleHash of
// key_of(block + k). Before a block is handed on, its keys are hashed and their slots are prefetched in each of
// indexes. If on_block returns false, the loop stops. Returns true if every block ran.
template <typename KeyOf, typename OnBlock, typename... Indexes>
bool hashedBlockRuns(
    std::size_t first, std::size_t last, const KeyOf& key_of, const OnBlock& on_block, const Indexes&... indexes) {
  std::array<std::size_t, kHashBlock> hashes{};
  for (std::size_t block = first; block < last; block += kHashBlock) {
    const std::size_t end = std::min(last, block + kHashBlock);
    for (std::size_t i = block; i < end; ++i) {
      hashes[i - block] = TupleHash{}(key_of(i));
      (indexes.prefetch(hashes[i - block]), ...);
    }
    if (!on_block(hashes.data(), block, end - block)) { return false; }
  }
  return true;
}

// Calls step(i, hash) for each i in [first, last) in order. hash is the TupleHash of key_of(i), computed a block
// ahead by hashedBlockRuns. If step returns false, the loop stops. Returns true if every step ran.
template <typename KeyOf, typename Step, typename... Indexes>
bool hashedBlocks(
    std::size_t first, std::size_t last, const KeyOf& key_of, const Step& step, const Indexes&... indexes) {
  const auto each = [&step](const std::size_t* hashes, std::size_t block, std::size_t count) {
    for (std::size_t k = 0; k < count; ++k) {
      if (!step(block + k, hashes[k])) { return false; }
    }
    return true;
  };
  return hashedBlockRuns(first, last, key_of, each, indexes...);
}

// The slots of a FlatIndex and everything that the index does without reading a key: sizing, tags, and placing keys
// that are known to be distinct by their hashes alone. The class is not a template, so the indexes of all key types
// share one copy of its code.
class FlatSlots {
 public:
  static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

  // Starts to load the slot at which a probe for a key with this TupleHash starts.
  void prefetch(std::size_t hash) const noexcept {
    if (!slots_.empty()) { prefetchForWrite(slots_.data() + (hash & mask_)); }
  }

  // Places the entries first to first + count - 1 under hashes[0] to hashes[count - 1], by their hashes alone,
  // without reading a key. The keys must differ from each other and from every key in the table, as the rows of a
  // set do. The table must already have room for them, as FlatIndex(count) makes it.
  void placeDistinct(const std::size_t* hashes, std::size_t first, std::size_t count) {
    if (first + count > kEmpty) { throw std::length_error("dreamor: more than 2^32 - 2 rows to index."); }
    if ((size_ + count) * 2 > slots_.size()) {
      throw std::logic_error("dreamor: FlatSlots::placeDistinct past the room the table was made with.");
    }
    if (count == 0) { return; }
    checkEntry(first + count - 1);
    for (std::size_t i = 0; i < count; ++i) {
      prefetchForWrite(slots_.data() + (hashes[i] & mask_));
    }
    for (std::size_t i = 0; i < count; ++i) {
      std::size_t slot = hashes[i] & mask_;
      while (slots_[slot] != kEmpty) {
        slot = (slot + 1) & mask_;
      }
      slots_[slot] = tagOf(hashes[i]) | static_cast<std::uint32_t>(first + i);
    }
    size_ += count;
  }

 protected:
  FlatSlots() = default;

  // Makes room for `count` entries before the table first grows.
  explicit FlatSlots(std::size_t count) { resize(capacityFor(count)); }

  static constexpr std::uint32_t kEmpty     = 0xffffffffU;
  static constexpr std::size_t kMinCapacity = 16;  // A power of two, as every capacity is.
  static constexpr unsigned kTaggedFrom     = 16;  // The log2 of the smallest capacity that has tags.
  static constexpr unsigned kSlotBits       = std::numeric_limits<std::uint32_t>::digits;

  static void prefetchForWrite([[maybe_unused]] const void* address) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(address, 1);
#endif
  }

  [[nodiscard]] static std::size_t capacityFor(std::size_t count) {
    std::size_t capacity = kMinCapacity;
    while (capacity < count * 2) {
      capacity *= 2;
    }
    return capacity;
  }

  // An entry number shares its slot with a tag, so it must be less than the capacity minus one. Every caller numbers
  // its entries from 0 and sizes the table for them, which keeps them below half the capacity. A debug build checks.
  void checkEntry([[maybe_unused]] std::size_t entry) const {
#ifndef NDEBUG
    if (entry + 1 >= slots_.size()) { throw std::logic_error("dreamor: FlatIndex entry number past its capacity."); }
#endif
  }

  // Makes `capacity` empty slots, and splits the 32 bits of a slot between the entry number and the tag. If the
  // table has no tags, because it is small or too large to spare bits, the tag is 0 and the mask keeps every bit.
  // Then the same probe compares every key.
  void resize(std::size_t capacity) {
    slots_.assign(capacity, kEmpty);
    mask_       = capacity - 1;
    entry_mask_ = entryMaskFor(capacity);
  }

  // Returns the bits of a slot that hold the entry number, for a table of `capacity` slots.
  [[nodiscard]] static std::uint32_t entryMaskFor(std::size_t capacity) noexcept {
    const auto entry_bits = static_cast<unsigned>(std::countr_zero(capacity));
    const bool tagged     = entry_bits >= kTaggedFrom && entry_bits < kSlotBits;
    return tagged ? (std::uint32_t{1} << entry_bits) - 1U : kEmpty;
  }

  // Returns the tag of a key with this hash: the top bits of the hash, placed in the bits of a slot above the entry
  // number. In a table without tags, the entry mask keeps every bit, so the tag is 0.
  [[nodiscard]] static std::uint32_t tagFor(std::size_t hash, std::uint32_t entry_mask) noexcept {
    constexpr unsigned kHashBits = std::numeric_limits<std::size_t>::digits;
    return static_cast<std::uint32_t>(hash >> (kHashBits - kSlotBits)) & ~entry_mask;
  }

  [[nodiscard]] std::uint32_t tagOf(std::size_t hash) const noexcept { return tagFor(hash, entry_mask_); }

 private:
  // A FlatIndex probes and grows these slots itself, because it reads its keys. No other class touches the slots.
  template <typename Key>
  friend class FlatIndex;

  std::vector<std::uint32_t> slots_;
  std::size_t mask_         = 0;
  std::size_t size_         = 0;
  std::uint32_t entry_mask_ = kEmpty;
};

template <typename Key>
class FlatIndex : public FlatSlots {
 public:
  FlatIndex() = default;

  explicit FlatIndex(std::size_t count) : FlatSlots{count} {}

  template <typename KeyAt>
  [[nodiscard]] std::size_t find(const Key& key, const KeyAt& key_at) const {
    return find(key, TupleHash{}(key), key_at);
  }

  // find() for a key whose hash is already known, as in hashedBlocks().
  template <typename KeyAt>
  [[nodiscard]] std::size_t find(const Key& key, std::size_t hash, const KeyAt& key_at) const {
    if (slots_.empty()) { return kNotFound; }
    return probe(key, hash, key_at);
  }

  // find() for a key of another type that hashes and compares as Key does, without making a Key from it. An example
  // is a std::string_view for a std::string key: std::hash returns the same hash for both.
  template <typename Probe, typename KeyAt>
  [[nodiscard]] std::size_t findEquivalent(const Probe& key, const KeyAt& key_at) const {
    if (slots_.empty()) { return kNotFound; }
    const auto [slot, found] = seek(key, TupleHash{}(key), key_at);
    return found ? (slots_[slot] & entry_mask_) : kNotFound;
  }

  // Inserts key as `entry`. Returns the entry that already holds key, or kNotFound if `entry` now holds it.
  // key_at(entry) must return key from the next insert on, because growing rehashes every entry through key_at.
  template <typename KeyAt>
  std::size_t insert(const Key& key, std::size_t entry, const KeyAt& key_at) {
    return insert(key, TupleHash{}(key), entry, key_at);
  }

  // insert() for a key whose hash is already known, as in hashedBlocks().
  template <typename KeyAt>
  std::size_t insert(const Key& key, std::size_t hash, std::size_t entry, const KeyAt& key_at) {
    if (entry >= kEmpty) { throw std::length_error("dreamor: more than 2^32 - 2 rows to index."); }
    if ((size_ + 1) * 2 > slots_.size()) { grow(key_at); }
    checkEntry(entry);
    return place(key, hash, entry, key_at);
  }

  // Inserts the entries first to last - 1, each under key_at(entry), as insert() would insert them one by one. The
  // keys are hashed a block ahead, by hashedBlocks(). If a key is already held, insertAll() stops and returns {the
  // entry that holds the key, the entry that repeats it}. Otherwise, it returns {kNotFound, kNotFound}.
  template <typename KeyAt>
  std::pair<std::size_t, std::size_t> insertAll(std::size_t first, std::size_t last, const KeyAt& key_at) {
    if (last > kEmpty) { throw std::length_error("dreamor: more than 2^32 - 2 rows to index."); }
    while ((size_ + last - first) * 2 > slots_.size()) {
      grow(key_at);  // Grows before any insert, so that no prefetch is for a table that is about to be replaced.
    }
    if (last > first) { checkEntry(last - 1); }
    std::pair<std::size_t, std::size_t> repeat{kNotFound, kNotFound};
    hashedBlocks(
        first,
        last,
        key_at,
        [&](std::size_t entry, std::size_t hash) {
          const std::size_t held = place(key_at(entry), hash, entry, key_at);
          if (held != kNotFound) { repeat = {held, entry}; }
          return held == kNotFound;
        },
        *this);
    return repeat;
  }

 private:
  // Returns the slot at which a probe for key stops, and whether the key is there. The slot is the one that holds
  // the key, or the empty slot that ends the probe. The key of a slot is read only if its tag agrees. One probe
  // serves tables with and without tags. Two copies would make find() too large to inline into its callers, and a
  // short lookup, such as Ordered's prev(t), would then pay for one or two calls.
  template <typename Probe, typename KeyAt>
  [[nodiscard]] DREAMOR_INLINE std::pair<std::size_t, bool> seek(const Probe& key,
                                                                 std::size_t hash,
                                                                 const KeyAt& key_at) const {
    const std::uint32_t tag = tagOf(hash);
    for (std::size_t slot = hash & mask_;; slot = (slot + 1) & mask_) {
      const std::uint32_t held = slots_[slot];
      if (held == kEmpty) { return {slot, false}; }
      if ((held & ~entry_mask_) == tag && key_at(held & entry_mask_) == key) { return {slot, true}; }
    }
  }

  template <typename KeyAt>
  [[nodiscard]] DREAMOR_INLINE std::size_t probe(const Key& key, std::size_t hash, const KeyAt& key_at) const {
    const auto [slot, found] = seek(key, hash, key_at);
    return found ? (slots_[slot] & entry_mask_) : kNotFound;
  }

  // insert() once there is room. The caller has already checked the entry and grown the table.
  template <typename KeyAt>
  std::size_t place(const Key& key, std::size_t hash, std::size_t entry, const KeyAt& key_at) {
    const auto [slot, found] = seek(key, hash, key_at);
    if (found) { return slots_[slot] & entry_mask_; }
    slots_[slot] = tagOf(hash) | static_cast<std::uint32_t>(entry);
    ++size_;
    return kNotFound;
  }

  // The grown slots are filled before they replace the old ones. If the allocation or key_at throws, the table is
  // as it was, as std::vector's reallocation leaves the vector.
  template <typename KeyAt>
  void grow(const KeyAt& key_at) {
    const std::size_t capacity = slots_.empty() ? kMinCapacity : slots_.size() * 2;
    std::vector<std::uint32_t> grown(capacity, kEmpty);
    const std::size_t mask         = capacity - 1;
    const std::uint32_t entry_mask = entryMaskFor(capacity);
    for (const std::uint32_t held : slots_) {
      if (held == kEmpty) { continue; }
      const std::uint32_t entry = held & entry_mask_;
      const std::size_t hash    = TupleHash{}(key_at(entry));
      std::size_t slot          = hash & mask;
      while (grown[slot] != kEmpty) {
        slot = (slot + 1) & mask;
      }
      grown[slot] = tagFor(hash, entry_mask) | entry;
    }
    slots_      = std::move(grown);
    mask_       = mask;
    entry_mask_ = entry_mask;
  }
};

}  // namespace dreamor::internal::inline base
