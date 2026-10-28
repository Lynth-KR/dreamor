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
// Reading a key is a load of its own, and on a large table a cache miss; a probe past a slot
// that holds another key would read that key for nothing, and a key that is not there would read
// one for every occupied slot it passes. So a slot also holds a tag: an entry number needs only
// as many bits as the capacity has (2^c slots, entries below 2^c - 1), and the other 32 - c bits
// carry the top bits of the key's hash. A probe reads a key only where the tags agree - one in
// 2^(32 - c) of the slots that hold another key, with the slot picked by the hash's low bits.
// A table small enough to sit in a core's cache (under 2^16 slots) reads keys that are in cache
// too, and the tags would only add work to every probe: it has none. Neither have tables of 2^32
// slots and more, which have no bits to spare. Without tags, every slot's tag and every key's is 0:
// the one probe serves both, its tag check always passing.
//
// On a large table every probe is a cache miss, and one key after another they would come one
// after another. Where a run of keys is known up front, hashedBlocks() hashes a block of them,
// prefetches their slots in every index the caller names, and then hands each key's hash to
// the caller's find() and insert(): the block's misses overlap. On 1e7 rows that is about half
// the time.
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
      if (!step(i, hashes[i - block])) { return false; }
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
  explicit FlatIndex(std::size_t count) { resize(capacityFor(count)); }

  // Starts loading the slot a key with this hash - TupleHash's - starts its probe at.
  void prefetch(std::size_t hash) const noexcept {
    if (!slots_.empty()) { prefetchForWrite(slots_.data() + (hash & mask_)); }
  }

  template <typename KeyAt>
  [[nodiscard]] std::size_t find(const Key& key, const KeyAt& key_at) const {
    return find(key, TupleHash{}(key), key_at);
  }
  // find() with the key's hash already known (hashedBlocks()).
  template <typename KeyAt>
  [[nodiscard]] std::size_t find(const Key& key, std::size_t hash, const KeyAt& key_at) const {
    if (slots_.empty()) { return kNotFound; }
    return probe(key, hash, key_at);
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
    if (entry >= kEmpty) { throw std::length_error("dreamor: more than 2^32 - 2 rows to index."); }
    if ((size_ + 1) * 2 > slots_.size()) { grow(key_at); }
    checkEntry(entry);
    return place(key, hash, entry, key_at);
  }

  // Entries first..last-1, each under key_at(entry), as insert() would put them one by one -
  // up to the first entry whose key is already held: {the entry holding it, the entry repeating
  // it}, or {kNotFound, kNotFound} once all are in. A block of hashes ahead (hashedBlocks()).
  template <typename KeyAt>
  std::pair<std::size_t, std::size_t> insertAll(std::size_t first, std::size_t last, const KeyAt& key_at) {
    if (last > kEmpty) { throw std::length_error("dreamor: more than 2^32 - 2 rows to index."); }
    while ((size_ + last - first) * 2 > slots_.size()) {
      grow(key_at);  // all at once, so that no prefetch is for a table about to be replaced
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
  static constexpr std::uint32_t kEmpty     = 0xffffffffU;
  static constexpr std::size_t kMinCapacity = 16;  // A power of two, like every capacity.
  static constexpr unsigned kTaggedFrom     = 16;  // log2 of the smallest capacity with tags
  static constexpr unsigned kSlotBits       = std::numeric_limits<std::uint32_t>::digits;

  static void prefetchForWrite([[maybe_unused]] const void* address) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(address, 1);
#endif
  }

  // The slot a probe for key stops at, and whether the key is there: the slot holding it, or the
  // empty one that ends the probe. A slot's key is read only where its tag agrees. One probe for
  // tables with tags and without, not one each: two copies make find() too large to inline into
  // its callers, and a short lookup - Ordered's prev(t) - then pays for a call or two.
  template <typename KeyAt>
  [[nodiscard, gnu::always_inline]] std::pair<std::size_t, bool> seek(const Key& key,
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
  [[nodiscard, gnu::always_inline]] std::size_t probe(const Key& key, std::size_t hash, const KeyAt& key_at) const {
    const auto [slot, found] = seek(key, hash, key_at);
    return found ? (slots_[slot] & entry_mask_) : kNotFound;
  }

  // insert() once there is room: the caller has checked the entry and grown the table.
  template <typename KeyAt>
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the hash and the entry, in insert()'s order
  std::size_t place(const Key& key, std::size_t hash, std::size_t entry, const KeyAt& key_at) {
    const auto [slot, found] = seek(key, hash, key_at);
    if (found) { return slots_[slot] & entry_mask_; }
    slots_[slot] = tagOf(hash) | static_cast<std::uint32_t>(entry);
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

  // An entry number shares its slot with a tag, so it must stay below the capacity less one. Every
  // caller numbers its entries from 0 up and sizes the table for them, which keeps them below
  // half the capacity; a debug build checks.
  void checkEntry([[maybe_unused]] std::size_t entry) const {
#ifndef NDEBUG
    if (entry + 1 >= slots_.size()) { throw std::logic_error("dreamor: FlatIndex entry number past its capacity."); }
#endif
  }

  // capacity empty slots, and the split of a slot's 32 bits between entry number and tag.
  // Without tags (a small table, or one too large to spare bits), the tag is 0 and the mask
  // keeps every bit, so the same probe compares every key.
  void resize(std::size_t capacity) {
    slots_.assign(capacity, kEmpty);
    mask_                 = capacity - 1;
    const auto entry_bits = static_cast<unsigned>(std::countr_zero(capacity));
    const bool tagged     = entry_bits >= kTaggedFrom && entry_bits < kSlotBits;
    entry_mask_           = tagged ? (std::uint32_t{1} << entry_bits) - 1U : kEmpty;
  }

  // The tag of a key with this hash, in the bits of a slot above the entry number: the hash's top
  // bits. 0 in a table without tags, whose entry mask keeps every bit.
  [[nodiscard]] std::uint32_t tagOf(std::size_t hash) const noexcept {
    constexpr unsigned kHashBits = std::numeric_limits<std::size_t>::digits;
    return static_cast<std::uint32_t>(hash >> (kHashBits - kSlotBits)) & ~entry_mask_;
  }

  template <typename KeyAt>
  void grow(const KeyAt& key_at) {
    const std::vector<std::uint32_t> old = std::move(slots_);
    const std::uint32_t old_mask         = entry_mask_;
    resize(old.empty() ? kMinCapacity : old.size() * 2);
    for (const std::uint32_t held : old) {
      if (held == kEmpty) { continue; }
      const std::uint32_t entry = held & old_mask;
      const std::size_t hash    = TupleHash{}(key_at(entry));
      std::size_t slot          = hash & mask_;
      while (slots_[slot] != kEmpty) {
        slot = (slot + 1) & mask_;
      }
      slots_[slot] = tagOf(hash) | entry;
    }
  }

  std::vector<std::uint32_t> slots_;
  std::size_t mask_         = 0;
  std::size_t size_         = 0;
  std::uint32_t entry_mask_ = kEmpty;
};

}  // namespace dreamor::general
