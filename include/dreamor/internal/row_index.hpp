// SPDX-License-Identifier: Apache-2.0
// File: internal/row_index.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// From a set's row to its position, and (row, X) iteration beside the set: what every
// variable table and value table shares, whatever the backend.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "dreamor/general/tuple_hash.hpp"

namespace dreamor::internal {

// Open addressing (linear probing) from a key to its row in a set. Narrow on purpose - what
// makes a general hash map hard is not needed here:
//   - built once from the whole set, never changed;
//   - load factor <= 1/2, so an empty slot always ends a probe and probes stay short;
//   - TupleHash already avalanches (splitmix64), so linear probing does not cluster;
//   - slots hold uint32 row numbers: 4 bytes each, up to 2^32 - 2 rows.
//
// Keys are not stored here: a slot holds a row number and the probe compares against the
// set's row, so the set's tuples are the only copy.
template <typename Key>
class FlatIndex {
 public:
  static constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);

  // Throws on a repeated row: one key with two variables is a modeling error, and which of
  // them a lookup would return is arbitrary.
  explicit FlatIndex(const std::vector<Key>& rows) {
    if (rows.size() >= kEmpty) {
      throw std::length_error("VarTable: more than 2^32 - 2 index tuples.");
    }
    std::size_t capacity = kMinCapacity;
    while (capacity < rows.size() * 2) {
      capacity *= 2;
    }
    slots_.assign(capacity, kEmpty);
    mask_ = capacity - 1;
    for (std::size_t row = 0; row < rows.size(); ++row) {
      std::size_t slot = home(rows[row]);
      for (; slots_[slot] != kEmpty; slot = (slot + 1) & mask_) {
        if (rows[slots_[slot]] == rows[row]) {
          throw std::invalid_argument("VarTable: the set repeats row " + std::to_string(slots_[slot]) + " at row " +
                                      std::to_string(row) + "; build it from S.distinct().");
        }
      }
      slots_[slot] = static_cast<std::uint32_t>(row);
    }
  }

  [[nodiscard]] std::size_t find(const Key& key, const std::vector<Key>& rows) const {
    for (std::size_t slot = home(key);; slot = (slot + 1) & mask_) {
      const std::uint32_t row = slots_[slot];
      if (row == kEmpty) {
        return kNotFound;
      }
      if (rows[row] == key) {
        return row;
      }
    }
  }

 private:
  static constexpr std::uint32_t kEmpty     = 0xffffffffU;
  static constexpr std::size_t kMinCapacity = 16;  // A power of two, like every capacity.

  [[nodiscard]] std::size_t home(const Key& key) const noexcept { return ::dreamor::general::TupleHash{}(key)&mask_; }

  std::vector<std::uint32_t> slots_;
  std::size_t mask_ = 0;
};

// Iterates (row, X) pairs: a set's rows beside a vector of X in the same order.
template <typename Key, typename X>
class RowPairIterator {
 public:
  using iterator_concept  = std::forward_iterator_tag;
  using iterator_category = std::forward_iterator_tag;
  using value_type        = std::pair<const Key&, X>;
  using difference_type   = std::ptrdiff_t;
  using reference         = value_type;

  RowPairIterator() = default;
  RowPairIterator(const Key* row, const X* x) noexcept : row_{row}, x_{x} {}

  [[nodiscard]] reference operator*() const { return {*row_, *x_}; }
  RowPairIterator& operator++() noexcept {
    ++row_;
    ++x_;
    return *this;
  }
  RowPairIterator operator++(int) noexcept {
    RowPairIterator before = *this;
    ++*this;
    return before;
  }
  [[nodiscard]] friend bool operator==(const RowPairIterator& a, const RowPairIterator& b) noexcept {
    return a.x_ == b.x_;
  }

 private:
  const Key* row_ = nullptr;
  const X* x_     = nullptr;
};

}  // namespace dreamor::internal
