// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// RowIndex, which finds the position of a set's row from its key, and RowPairIterator, which iterates (row, X) pairs
// beside the set. Every table of variables and every table of values uses them, whatever the backend.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/base/flat_index.hpp"
#include "dreamor/internal/base/inline.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/internal/index_set/ref.hpp"
#include "dreamor/logger.hpp"

namespace dreamor::internal::inline backend {

// The row on which sumExpr is computing its term, while it does so. A lookup by columns inside the term receives copies
// of the row's columns and has lost the row itself, so this row is a hint for that lookup. The row is only a hint: a
// table uses it if the row is one of the table's own and holds exactly the columns asked for, and hashes otherwise. A
// stale row or a row of another set therefore costs a comparison, never a wrong answer. The row is kept per thread, so
// that tables used on several threads do not see each other's rows.
[[nodiscard]] inline const void*& currentRow() noexcept {
  static constinit thread_local const void* row = nullptr;
  return row;
}

// Publishes a row for the duration of one call of the term, then restores the row that was there before, because a
// term may run a sumExpr of its own.
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

// The row that the last lookup by columns found on this thread, as a hint for the next lookup. A model often looks a
// key up twice, first to check whether a table has the key and then to read its variable. A model also walks a set
// and looks up each row's neighbour. The next answer is therefore often the same row, or, during a walk, the row
// after it. Like currentRow(), the hint is checked against the key and never trusted.
struct LastRow {
  const void* row = nullptr;
  bool walking    = false;  // Whether the next lookup also tries the row after this one.
};

// Four hints, each for a different table. A model takes turns between tables, as a row's constraint reads one family
// and then another, and with a single hint each lookup would find the other table's row there. The first hint belongs
// to the table looked up last, so a table used on its own costs no more than with a single hint. If a lookup finds
// its table's hint further back, it swaps that hint to the front. If no hint belongs to the table, the lookup puts the
// row it finds in front, and the last hint is dropped.
using LastRows = std::array<LastRow, 4>;

[[nodiscard]] inline LastRows& lastRows() noexcept {
  alignas(sizeof(LastRows)) static constinit thread_local LastRows last{};  // 64 bytes, which is one cache line.
  return last;
}

#ifndef NDEBUG
// Reports a table's set that holds a row twice. This is an internal check, not a user's contract: build() checks
// every source, and dreamor's operations drop the repeats that they would make (select, unite). A set that reaches a
// table therefore holds each row once, as the release build's placeDistinct assumes. The function is out of line and
// not a template, as set.hpp's reportRepeat is, so that every table's index shares one copy of the message and its
// format is not checked once for each row type.
[[noreturn]] inline void reportTableRepeat(std::size_t first, std::size_t repeat) {
  DREAMOR_DEBUG_FATAL(
      "dreamor: a table's set repeats row {} at row {}, so a range the set was built from holds an "
      "element twice - a broken contract (dreamor/index_set.hpp).",
      first,
      repeat);
}
#endif

// Finds the position of a set's row from its key, with a FlatIndex over the set's rows, which the set keeps. The index
// holds 4 bytes for each slot and no copy of a key. The first lookup that needs the hash builds it, not the table's
// constructor, so a table that is reached only by row never pays for the hash: a walk over its set or a group, or a
// sum with the table as its term.
template <typename Key>
class RowIndex {
 public:
  static constexpr std::size_t kNotFound = ::dreamor::internal::FlatIndex<Key>::kNotFound;

  explicit RowIndex(const Rows<Key>& rows) {
    if (rows.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error("dreamor: a table's set has more than 2^32 - 2 rows.");
    }
    if constexpr (kOneEntity) {
      if (rows.empty()) { return; }
      const auto first = addressOf(rows.front());
      for (std::size_t i = 0; i < rows.size(); ++i) {
        if (addressOf(rows[i]) != first + (i * kEntitySize)) { return; }
      }
      first_ = first;
    }
  }

  // Returns the position of a key, given as a caller writes it: its columns, or one row. Where the row is known to be
  // one of rows, no hash is computed:
  //   - If the key is one row passed by reference, it may be a row of the set that the table was built on, reached by
  //     a walk over that set or one of its groups. Only its address tells.
  //   - If the key is columns, they are copies. The row they came from may be the row that sumExpr published (see
  //     currentRow()), or else the row that the last lookup found or the row after it (see lastRows()).
  // Which of the two cases applies is known at compile time. If the set's one column refers to the elements of an
  // array, all of them in order, as for a frozen EntityVector taken whole, an entity's row is found from the entity's
  // address alone, as hand-written code reads values[i].
  template <typename... Args>
  [[nodiscard]] std::size_t find(const Rows<Key>& rows, const Args&... key) const {
    if constexpr (isExactRow<Key, Args...>()) {
      const std::size_t at = positionOf(rows, std::addressof(key...));
      return at != kNotFound ? at : built(rows).find(key..., rowsAt(rows));
    } else {
      if constexpr (kOneEntity && sizeof...(Args) == 1) {
        if (first_ != 0) { return entityPosition(rows, key...); }
      }
      const std::size_t at = positionOf(rows, currentRow());
      if (at != kNotFound && holds(rows[at], key...)) { return at; }
      return findByColumns(rows, key...);
    }
  }

 private:
  // Whether the key is one column of Refs, whose rows may be the elements of an EntityVector, in order.
  [[nodiscard]] static consteval bool oneEntity() {
    if constexpr (std::tuple_size_v<Key> == 1) { return ref<typename std::tuple_element_t<0, Key>::value_type>; }
    return false;
  }

  static constexpr bool kOneEntity         = oneEntity();
  static constexpr std::size_t kEntitySize = [] {
    if constexpr (kOneEntity) {
      return sizeof(typename std::tuple_element_t<0, Key>::value_type::element_type);
    } else {
      return std::size_t{1};
    }
  }();

  // Returns the address that a row of one column refers to, as an integer.
  [[nodiscard]] static std::uintptr_t addressOf(const Key& row) noexcept {
    return std::bit_cast<std::uintptr_t>(RefAccess::address(std::get<0>(row).get()));
  }

  // Returns an entity's row if the rows refer to an array's elements in order: the entity's offset from the first
  // element. Returns kNotFound for an address that is not one of the elements.
  template <typename Column>
  [[nodiscard]] std::size_t entityPosition(const Rows<Key>& rows, const Column& key) const {
    const std::uintptr_t offset = std::bit_cast<std::uintptr_t>(RefAccess::address(key.get())) - first_;
    const std::size_t at        = offset / kEntitySize;
    return offset % kEntitySize == 0 && at < rows.size() ? at : kNotFound;
  }

  // Returns the position of the row that an address falls in, or kNotFound if the address is not inside rows.
  // std::less<> also orders pointers into different arrays. If a row that sumExpr published, or a row passed in,
  // falls inside the range, it is one of the elements of rows. A hint of lastRows() need not be: the hints belong to
  // every Key on the thread, and a hint left by a destroyed set can fall inside a row of a set of another Key that now
  // uses its memory. The offset is therefore divided in bytes, which rounds it down to the start of its row. A
  // difference of Key pointers would assume a whole number of rows, and the compiler computes it as a multiplication
  // by the inverse of sizeof(Key). For an address in the middle of a row of 24 bytes, that gives a position past the
  // rows.
  [[nodiscard]] static std::size_t positionOf(const Rows<Key>& rows, const void* row) {
    const Key* const first = rows.data();
    const std::less<> before;
    if (row == nullptr || before(row, first) || !before(row, first + rows.size())) { return kNotFound; }
    const std::uintptr_t offset = std::bit_cast<std::uintptr_t>(row) - std::bit_cast<std::uintptr_t>(first);
    return offset / sizeof(Key);  // std::uintptr_t has the width of std::size_t on every supported platform.
  }

  // Checks whether the row's columns are exactly the ones asked for.
  template <typename... Columns>
  [[nodiscard]] static bool holds(const Key& row, const Columns&... key) {
    return ((std::get<Columns>(row) == key) && ...);
  }

  // Returns the position of a key given as columns, if no published row holds it. The lookup tries the last row found,
  // then, during a walk, the row after it, and then the hash. The row after is read only during a walk, because for
  // scattered lookups it is a cache miss that seldom finds the key. Before the hash is built, a lookup with no hint
  // tries the first row: a walk over the set starts there, and then finds every later row without the hash. The rows
  // are distinct, so a row that holds the key is the key's row.
  // This function is out of line, so that find(), which checks the published row and calls this function, stays
  // small enough to be inlined into a sumExpr term, where the published row answers nearly every lookup. Other
  // arrangements of the hint check were measured, and none was faster across the shapes of lookups.
  template <typename... Columns>
  [[nodiscard]] DREAMOR_NOINLINE std::size_t findByColumns(const Rows<Key>& rows, const Columns&... key) const {
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

  [[nodiscard]] static auto rowsAt(const Rows<Key>& rows) {
    return [&rows](std::size_t row) -> const Key& { return rows[row]; };
  }

  // Returns the hash, which the first lookup that needs it builds once; other lookups wait for it. A set holds no row
  // twice (source_holds_no_repeat), so each key has one row. The rows are therefore placed by their hashes alone
  // (FlatSlots::placeDistinct), with no row compared with another. They get the same slots that insertAll would give
  // them, for less work, and one copy of the placing code serves every row type. A debug build compares the rows
  // anyway, to stop on a repeated row.
  [[nodiscard]] const ::dreamor::internal::FlatIndex<Key>& built(const Rows<Key>& rows) const {
    if (!ready_.load(std::memory_order_acquire)) {
      std::call_once(once_, [&] {
        ::dreamor::internal::FlatIndex<Key> index{rows.size()};
#ifdef NDEBUG
        const auto place = [&index](const std::size_t* hashes, std::size_t block, std::size_t count) {
          index.placeDistinct(hashes, block, count);
          return true;
        };
        ::dreamor::internal::hashedBlockRuns(0, rows.size(), rowsAt(rows), place);
#else
        const auto [first, repeat] = index.insertAll(0, rows.size(), rowsAt(rows));
        if (first != kNotFound) { reportTableRepeat(first, repeat); }
#endif
        index_ = std::move(index);
        ready_.store(true, std::memory_order_release);
      });
    }
    return index_;
  }

  mutable std::once_flag once_;
  mutable std::atomic<bool> ready_{false};
  mutable ::dreamor::internal::FlatIndex<Key> index_;
  std::uintptr_t first_ = 0;  // The first row's entity if the rows refer to an array's elements in order; else 0.
};

// Iterates (row, X) pairs: a set's rows beside their X values, in the same order. Cursor walks the X values: it is a
// pointer into an array of them, or a type that steps through X values that a table computes from the position.
template <typename Key, typename Cursor>
class RowPairIterator {
 public:
  using iterator_concept  = std::forward_iterator_tag;
  using iterator_category = std::forward_iterator_tag;
  using value_type        = std::pair<const Key&, std::remove_cvref_t<decltype(*std::declval<const Cursor&>())>>;
  using difference_type   = std::ptrdiff_t;
  using reference         = value_type;

  RowPairIterator() = default;

  RowPairIterator(const Key* row, Cursor x) noexcept : row_{row}, x_{x} {}

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
    return a.row_ == b.row_;
  }

 private:
  const Key* row_ = nullptr;
  Cursor x_{};
};

// Returns the row index of a set. Every table on the set shares one index, which the first table makes: its families
// of variables, its ParamTables and its SolutionTables. The index builds its hash at the first lookup that needs it.
template <typename... Keys>
[[nodiscard]] std::shared_ptr<const RowIndex<std::tuple<Keys...>>> sharedRowIndex(const IndexSet<Keys...>& keys) {
  using Key = std::tuple<Keys...>;
  return SetAccess::indexSlot(keys).template get<RowIndex<Key>>(
      [&keys] { return std::make_shared<const RowIndex<Key>>(SetAccess::rows(keys)); });
}

}  // namespace dreamor::internal::inline backend
