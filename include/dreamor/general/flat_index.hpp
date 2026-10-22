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
//   - TupleHash already avalanches (splitmix64), so linear probing does not cluster;
//   - slots hold uint32 entry numbers: 4 bytes each, up to 2^32 - 2 entries.
//
// Keys are not stored here. The caller keeps them - a set's rows, a list of group keys - and
// passes key_at(entry) to every call, so a slot is 4 bytes and the caller's storage stays
// the only copy. key_at may hand back a reference or, for a projection, a value.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dreamor/general/tuple_hash.hpp"

namespace dreamor::general {

template <typename Key>
class FlatIndex {
 public:
  static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

  FlatIndex() = default;

  // Room for `count` entries before the first growth.
  explicit FlatIndex(std::size_t count) : slots_(capacityFor(count), kEmpty), mask_{slots_.size() - 1} {}

  template <typename KeyAt>
  [[nodiscard]] std::size_t find(const Key& key, const KeyAt& key_at) const {
    if (slots_.empty()) {
      return kNotFound;
    }
    for (std::size_t slot = home(key);; slot = (slot + 1) & mask_) {
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
    if (entry >= kEmpty) {
      throw std::length_error("dreamor: more than 2^32 - 2 rows to index.");
    }
    if ((size_ + 1) * 2 > slots_.size()) {
      grow(key_at);
    }
    std::size_t slot = home(key);
    for (; slots_[slot] != kEmpty; slot = (slot + 1) & mask_) {
      if (key_at(slots_[slot]) == key) {
        return slots_[slot];
      }
    }
    slots_[slot] = static_cast<std::uint32_t>(entry);
    ++size_;
    return kNotFound;
  }

 private:
  static constexpr std::uint32_t kEmpty     = 0xffffffffU;
  static constexpr std::size_t kMinCapacity = 16;  // A power of two, like every capacity.

  [[nodiscard]] static std::size_t capacityFor(std::size_t count) {
    std::size_t capacity = kMinCapacity;
    while (capacity < count * 2) {
      capacity *= 2;
    }
    return capacity;
  }

  [[nodiscard]] std::size_t home(const Key& key) const noexcept { return TupleHash{}(key)&mask_; }

  template <typename KeyAt>
  void grow(const KeyAt& key_at) {
    const std::vector<std::uint32_t> old = std::move(slots_);
    slots_.assign(old.empty() ? kMinCapacity : old.size() * 2, kEmpty);
    mask_ = slots_.size() - 1;
    for (const std::uint32_t entry : old) {
      if (entry == kEmpty) {
        continue;
      }
      std::size_t slot = home(key_at(entry));
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
