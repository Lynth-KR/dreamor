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
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"

namespace dreamor::internal {

// From a set's row to its position: a FlatIndex over the set's rows, which the set keeps -
// the index holds 4 bytes a slot and no copy of a key.
template <typename Key>
class RowIndex {
 public:
  static constexpr std::size_t kNotFound = ::dreamor::general::FlatIndex<Key>::kNotFound;

  // Throws on a repeated row: one key with two variables is a modeling error, and which of
  // them a lookup would return is arbitrary.
  explicit RowIndex(const std::vector<Key>& rows) : index_{rows.size()} {
    if (rows.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error("VarTable: more than 2^32 - 2 index tuples.");
    }
    const auto at = rowsAt(rows);
    for (std::size_t row = 0; row < rows.size(); ++row) {
      const std::size_t first = index_.insert(rows[row], row, at);
      if (first != kNotFound) {
        throw std::invalid_argument("VarTable: the set repeats row " + std::to_string(first) + " at row " +
                                    std::to_string(row) + "; build it from S.distinct().");
      }
    }
  }

  [[nodiscard]] std::size_t find(const Key& key, const std::vector<Key>& rows) const {
    return index_.find(key, rowsAt(rows));
  }

 private:
  [[nodiscard]] static auto rowsAt(const std::vector<Key>& rows) {
    return [&rows](std::size_t row) -> const Key& { return rows[row]; };
  }

  ::dreamor::general::FlatIndex<Key> index_;
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
