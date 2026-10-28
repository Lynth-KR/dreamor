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

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"
#include "dreamor/index_set/key_args.hpp"
#include "dreamor/logger/logger_light.hpp"

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
// Four of them, each on its own table: model code takes turns between tables - x[l, c] <= y[l] -
// and with one hint each lookup would find the other table's row there. The first is the hint of
// the table looked up last, so one table on its own costs what a single hint did; a lookup that
// finds its table's hint further back swaps it to the front, and a table none is on goes in front
// when the lookup finds its row, the last hint dropping out.
using LastRows = std::array<LastRow, 4>;
[[nodiscard]] inline LastRows& lastRows() noexcept {
  alignas(sizeof(LastRows)) static constinit thread_local LastRows last{};  // 64 bytes: one cache line
  return last;
}

// From a set's row to its position: a FlatIndex over the set's rows, which the set keeps -
// the index holds 4 bytes a slot and no copy of a key. The hash is built by the first lookup
// that needs it, not with the table: a table reached only by row - walking its set or a group,
// or as a sum's term - never pays for it.
template <typename Key>
class RowIndex {
 public:
  static constexpr std::size_t kNotFound = ::dreamor::general::FlatIndex<Key>::kNotFound;

  explicit RowIndex(const std::vector<Key>& rows) {
    if (rows.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error("VarTable: more than 2^32 - 2 index tuples.");
    }
  }

  // A key as a caller writes it - its columns, or one row - to its position, without
  // hashing where the row is known to be one of rows:
  //   - one row handed in by reference: it may be a row of the set the table was built on,
  //     reached by walking that set or one of its groups. Only its address tells.
  //   - columns: they are copies, so the row they came from is the one sumOf published, if
  //     any - see currentRow() - or else the row the last lookup found, or the one either side
  //     of it - see lastRows().
  // Which of the two applies is known at compile time.
  template <typename... Args>
  [[nodiscard]] std::size_t find(const std::vector<Key>& rows, const Args&... key) const {
    if constexpr (isExactRow<Key, Args...>()) {
      const std::size_t at = positionOf(rows, std::addressof(key...));
      return at != kNotFound ? at : built(rows).find(key..., rowsAt(rows));
    } else {
      const std::size_t at = positionOf(rows, currentRow());
      if (at != kNotFound && holds(rows[at], key...)) { return at; }
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
    if (row == nullptr || before(row, first) || !before(row, first + rows.size())) { return kNotFound; }
    return static_cast<std::size_t>(static_cast<const Key*>(row) - first);
  }

  // The row's columns are exactly the ones asked for.
  template <typename... Columns>
  [[nodiscard]] static bool holds(const Key& row, const Columns&... key) {
    return ((std::get<Columns>(row) == key) && ...);
  }

  // Columns without the published row: the last row found; on a walk, the one after it; else
  // the hash. The row after is read only on a walk: on scattered lookups it is a cache miss
  // that seldom pays. Before the hash is built, a lookup with no row found yet tries the first
  // row: a walk over the set starts there, and finds every row after it without the hash.
  // Rows are distinct, so a row that holds the key is the key's row.
  // Out of line, so that find() - the published-row check and a call - stays small enough to
  // inline into a sumOf term, where the published row answers nearly every lookup.
  template <typename... Columns>
  [[nodiscard, gnu::noinline]] std::size_t findByColumns(const std::vector<Key>& rows, const Columns&... key) const {
    LastRows& hints  = lastRows();
    std::size_t last = positionOf(rows, hints.front().row);
    if (last == kNotFound) {
      for (std::size_t i = 1; i < hints.size(); ++i) {
        if (const std::size_t at = positionOf(rows, hints[i].row); at != kNotFound) {
          std::swap(hints.front(), hints[i]);
          last = at;
          break;
        }
      }
    }
    LastRow& hint = hints.front();
    if (last != kNotFound) {
      if (holds(rows[last], key...)) {
        hint.walking = true;
        return last;
      }
      if (hint.walking && last + 1 < rows.size() && holds(rows[last + 1], key...)) {
        hint.row = rows.data() + last + 1;
        return last + 1;
      }
    } else if (!ready_.load(std::memory_order_acquire) && !rows.empty() && holds(rows.front(), key...)) {
      std::move_backward(hints.begin(), hints.end() - 1, hints.end());
      hint = {.row = rows.data(), .walking = true};
      return 0;
    }
    const std::size_t found = built(rows).find(Key{key...}, rowsAt(rows));
    if (found != kNotFound) {
      if (last == kNotFound) { std::move_backward(hints.begin(), hints.end() - 1, hints.end()); }
      hint.walking = last != kNotFound && found == last + 1;
      hint.row     = rows.data() + found;
    }
    return found;
  }

  [[nodiscard]] static auto rowsAt(const std::vector<Key>& rows) {
    return [&rows](std::size_t row) -> const Key& { return rows[row]; };
  }

  // The hash, built once, by whichever lookup needs it first; the others wait for it. A set holds
  // no row twice (index_set.hpp), so each key has one variable. A debug build stops on a repeat
  // all the same: indexing finds it for nothing.
  [[nodiscard]] const ::dreamor::general::FlatIndex<Key>& built(const std::vector<Key>& rows) const {
    if (!ready_.load(std::memory_order_acquire)) {
      std::call_once(once_, [&] {
        ::dreamor::general::FlatIndex<Key> index{rows.size()};
        [[maybe_unused]] const auto [first, repeat] = index.insertAll(0, rows.size(), rowsAt(rows));
#ifndef NDEBUG
        if (first != kNotFound) {
          DREAMOR_DEBUG_FATAL(
              "VarTable: the set repeats row {} at row {}, so a range handed to makeSet holds an "
              "element twice - a broken contract (dreamor/index_set.hpp).",
              first,
              repeat);
        }
#endif
        index_ = std::move(index);
        ready_.store(true, std::memory_order_release);
      });
    }
    return index_;
  }

  mutable std::once_flag once_;
  mutable std::atomic<bool> ready_{false};
  mutable ::dreamor::general::FlatIndex<Key> index_;
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
