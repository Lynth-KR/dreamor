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

#include "dreamor/general/flat_index.hpp"
#include "dreamor/index_set/indexed.hpp"
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

  explicit RowIndex(const Rows<Key>& rows) {
    if (rows.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error("VarTable: more than 2^32 - 2 index tuples.");
    }
    if constexpr (kOneEntity) {
      if (rows.empty()) { return; }
      const auto first = std::bit_cast<std::uintptr_t>(std::get<0>(rows.front()).get());
      for (std::size_t i = 0; i < rows.size(); ++i) {
        if (std::bit_cast<std::uintptr_t>(std::get<0>(rows[i]).get()) != first + (i * kEntitySize)) { return; }
      }
      first_ = first;
    }
  }

  // A key as a caller writes it - its columns, or one row - to its position, without
  // hashing where the row is known to be one of rows:
  //   - one row handed in by reference: it may be a row of the set the table was built on,
  //     reached by walking that set or one of its groups. Only its address tells.
  //   - columns: they are copies, so the row they came from is the one sumOf published, if
  //     any - see currentRow() - or else the row the last lookup found, or the one either side
  //     of it - see lastRows().
  // Which of the two applies is known at compile time. And a set whose one column points at the
  // elements of an array, all of them in order - a frozen EntityVector taken whole - finds an
  // entity's row by its address alone, as hand-written code takes values[i].
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
  // One column of pointers to complete objects: its rows may be an array's elements, in order.
  [[nodiscard]] static consteval bool oneEntity() {
    if constexpr (std::tuple_size_v<Key> == 1) {
      using Value = std::tuple_element_t<0, Key>::Value;
      if constexpr (std::is_pointer_v<Value>) {
        return requires { sizeof(std::remove_pointer_t<Value>); };
      }
    }
    return false;
  }
  static constexpr bool kOneEntity         = oneEntity();
  static constexpr std::size_t kEntitySize = [] {
    if constexpr (kOneEntity) {
      return sizeof(std::remove_pointer_t<typename std::tuple_element_t<0, Key>::Value>);
    } else {
      return std::size_t{1};
    }
  }();

  // An entity's row when the rows are an array's elements in order: its offset from the first,
  // or kNotFound for an address that is not one of them.
  template <typename Column>
  [[nodiscard]] std::size_t entityPosition(const Rows<Key>& rows, const Column& key) const {
    const std::uintptr_t offset = std::bit_cast<std::uintptr_t>(key.get()) - first_;
    const std::size_t at        = offset / kEntitySize;
    return offset % kEntitySize == 0 && at < rows.size() ? at : kNotFound;
  }

  // The position of the row an address falls in, or kNotFound when it is not inside rows.
  // std::less<> orders pointers into different arrays too. Inside the range, a row sumOf published
  // or one handed in is one of rows' elements; a hint of lastRows() need not be: the hints are every
  // Key's on the thread, and one left by a set gone can fall inside a row of a set, of another Key,
  // that now has its memory. So the offset is divided in bytes, taking it down to its row's start: a
  // difference of Key pointers would assume a whole number of rows, which the compiler turns into a
  // multiply by sizeof(Key)'s inverse - a position past the rows for an address mid-row of 24 bytes.
  [[nodiscard]] static std::size_t positionOf(const Rows<Key>& rows, const void* row) {
    const Key* const first = rows.data();
    const std::less<> before;
    if (row == nullptr || before(row, first) || !before(row, first + rows.size())) { return kNotFound; }
    const std::uintptr_t offset = std::bit_cast<std::uintptr_t>(row) - std::bit_cast<std::uintptr_t>(first);
    return static_cast<std::size_t>(offset / sizeof(Key));
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
  //
  // Tried against this layout and dropped (measured per lookup shape, and over the catalog's
  // lookup and order families, GCC 13 and Clang 18):
  //   - the hints' check apart from the hash - the hash in a function of its own, or the check
  //     inline at the caller: a walk sheds the frame (82 -> 45 instructions on GCC), but the hash
  //     gains a call, its columns past the registers on the stack - scattered and absent keys
  //     +15% to +20%, more the more columns. Forcing the check inline moves the call up a level
  //     instead, and how far depends on the model's own code around the lookup.
  //   - a set's hint left in its place, not swapped to the front - held as the set's rows and a
  //     position - or swapped only on the way to the hash: turns between sets 15-20% faster,
  //     scattered and absent keys 4-9% slower.
  // One call a lookup, whatever the call site looks like: predictable, and nothing tried did
  // better across the shapes. Change it for a new idea, measured the same way, rather than for
  // another arrangement of these.
  template <typename... Columns>
  [[nodiscard, gnu::noinline]] std::size_t findByColumns(const Rows<Key>& rows, const Columns&... key) const {
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

  // The hash, built once, by whichever lookup needs it first; the others wait for it. A set holds
  // no row twice (index_set.hpp), so each key has one variable. A debug build stops on a repeat
  // all the same: indexing finds it for nothing.
  [[nodiscard]] const ::dreamor::general::FlatIndex<Key>& built(const Rows<Key>& rows) const {
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
  std::uintptr_t first_ = 0;  // the first row's entity when the rows are an array's elements in order, else 0
};

// Iterates (row, X) pairs: a set's rows beside their X, in the same order. Cursor walks the X: a
// pointer into an array of them, or whatever a table whose X are computed from the position steps.
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

// The row index of a set: every table on the set - vYield and vSetup on sPRODUCE, a ValueTable
// of data on it, the values a solve gives back - shares one, made by the first. Its hash waits
// for the first lookup that needs one.
template <typename Key>
[[nodiscard]] std::shared_ptr<const RowIndex<Key>> sharedRowIndex(const IndexSet<Key>& keys) {
  return SetAccess::indexSlot(keys).template get<RowIndex<Key>>(
      [&keys] { return std::make_shared<const RowIndex<Key>>(SetAccess::rows(keys)); });
}

}  // namespace dreamor::internal
