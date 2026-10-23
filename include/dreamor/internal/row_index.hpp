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
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"
#include "dreamor/index_set/key_args.hpp"

namespace dreamor::internal {

// The row sumOf is evaluating its term on, while it does: a hint for a lookup by columns
// inside the term - cost(m, i, t) * vYield(m, i, t) - which gets copies of the row's columns
// and so has lost the row itself. Only ever a hint: a table uses it when the row is one of
// its own and holds exactly the columns asked for, and hashes otherwise, so a stale or
// foreign row costs a comparison, never a wrong answer. Per thread, so tables built on
// several threads do not see each other's rows.
[[nodiscard]] inline const void*& currentRow() noexcept {
  static constinit thread_local const void* row = nullptr;
  return row;
}

// Publishes a row for the scope of one term call, and puts back whatever was there - a term
// may run a sumOf of its own.
class CurrentRowScope {
 public:
  explicit CurrentRowScope(const void* row) noexcept : saved_{currentRow()} { currentRow() = row; }
  ~CurrentRowScope() { currentRow() = saved_; }

  CurrentRowScope(const CurrentRowScope&)            = delete;
  CurrentRowScope& operator=(const CurrentRowScope&) = delete;
  CurrentRowScope(CurrentRowScope&&)                 = delete;
  CurrentRowScope& operator=(CurrentRowScope&&)      = delete;

 private:
  const void* saved_;
};

// The row the last lookup by columns found, on this thread: a hint for the next one. Model code
// asks for a key twice - contains(i, t), then vStock(i, t) - and walks a set asking for each
// row's neighbour - vStock(i, t - 1) beside (i, t) - so the next answer is often the same row or,
// on a walk, the one after it. A hint like currentRow(): checked against the key, never trusted.
struct LastRow {
  const void* row = nullptr;
  bool walking    = false;  // the last answer was the row before it or the one after that
};
[[nodiscard]] inline LastRow& lastRow() noexcept {
  static constinit thread_local LastRow last{};
  return last;
}

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
    const auto [first, repeat] = index_.insertAll(0, rows.size(), rowsAt(rows));
    if (first != kNotFound) {
      throw std::invalid_argument("VarTable: the set repeats row " + std::to_string(first) + " at row " +
                                  std::to_string(repeat) + "; build it from S.distinct().");
    }
  }

  // A key as a caller writes it - its columns, or one row - to its position, without
  // hashing where the row is known to be one of rows:
  //   - one row handed in by reference: it may be a row of the set the table was built on,
  //     reached by walking that set or one of its groups. Only its address tells.
  //   - columns: they are copies, so the row they came from is the one sumOf published, if
  //     any - see currentRow() - or else the row the last lookup found, or the one either side
  //     of it - see lastRow().
  // Which of the two applies is known at compile time.
  template <typename... Args>
  [[nodiscard]] std::size_t find(const std::vector<Key>& rows, const Args&... key) const {
    if constexpr (isExactRow<Key, Args...>()) {
      const std::size_t at = positionOf(rows, std::addressof(key...));
      return at != kNotFound ? at : index_.find(key..., rowsAt(rows));
    } else {
      const std::size_t at = positionOf(rows, currentRow());
      if (at != kNotFound && holds(rows[at], key...)) {
        return at;
      }
      return findByColumns(rows, key...);
    }
  }

 private:
  // The position of a row by its address, or kNotFound when it is not one of rows.
  // std::less<> orders pointers into different arrays too; inside the range, the pointer is
  // one of rows' elements.
  [[nodiscard]] static std::size_t positionOf(const std::vector<Key>& rows, const void* row) {
    const Key* const first = rows.data();
    const std::less<> before;
    if (row == nullptr || before(row, first) || !before(row, first + rows.size())) {
      return kNotFound;
    }
    return static_cast<std::size_t>(static_cast<const Key*>(row) - first);
  }

  // The row's columns are exactly the ones asked for.
  template <typename... Columns>
  [[nodiscard]] static bool holds(const Key& row, const Columns&... key) {
    return ((std::get<Columns>(row) == key) && ...);
  }

  // Columns without the published row: the last row found; on a walk, the one after it; else
  // the hash. The row after is read only on a walk: on scattered lookups it is a cache miss
  // that seldom pays. Rows are distinct, so a row that holds the key is the key's row.
  // Out of line, so that find() - the published-row check and a call - stays small enough to
  // inline into a sumOf term, where the published row answers nearly every lookup.
  template <typename... Columns>
  [[nodiscard, gnu::noinline]] std::size_t findByColumns(const std::vector<Key>& rows, const Columns&... key) const {
    LastRow& hint          = lastRow();
    const std::size_t last = positionOf(rows, hint.row);
    if (last != kNotFound) {
      if (holds(rows[last], key...)) {
        hint.walking = true;
        return last;
      }
      if (hint.walking && last + 1 < rows.size() && holds(rows[last + 1], key...)) {
        hint.row = rows.data() + last + 1;
        return last + 1;
      }
    }
    const std::size_t found = index_.find(Key{key...}, rowsAt(rows));
    if (found != kNotFound) {
      hint.walking = last != kNotFound && found == last + 1;
      hint.row     = rows.data() + found;
    }
    return found;
  }

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
