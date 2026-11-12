// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Grouped, which groups a set's rows by some of its columns and lays the groups out one after another, with Group
// and RowSlice.
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <numeric>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/base/flat_index.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"

// The message for a call of slice() or at() on a temporary Grouped.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_NAME_THE_GROUPED_MESSAGE                                                                      \
  "Name the Grouped first - const auto byMachine = S.groupBy<cMachine>(); - slice() and at() hand out views into it, " \
  "which would dangle once a temporary is gone. Walking it needs no name: for (... : S.groupBy<cMachine>()) is fine."

namespace dreamor {
template <typename Tup>
class RowSlice;
}  // namespace dreamor

namespace dreamor::internal::inline index_set {

// Gives the library access to a group's rows: the set's first row, and the positions of the group's rows in the
// set, in the set's order. A sum uses these positions to find a table's variables (sum_builder.hpp).
struct SliceAccess {
  template <typename Tup>
  [[nodiscard]] static const Tup* first(const RowSlice<Tup>& rows) noexcept {
    return rows.base_;
  }

  template <typename Tup>
  [[nodiscard]] static std::span<const std::uint32_t> positions(const RowSlice<Tup>& rows) noexcept {
    return {rows.first_, rows.count_};
  }
};

template <typename T>
struct IsRowSlice : std::false_type {};

template <typename Tup>
struct IsRowSlice<RowSlice<Tup>> : std::true_type {};

}  // namespace dreamor::internal::inline index_set

namespace dreamor {

/// @section Grouped
///
/// A `Grouped` divides the rows of a set into groups by the values of some of its columns, the key columns, for a
/// constraint or a sum over each group. `IndexSet::groupBy` returns a `Grouped`, which is read in two ways:
///
/// - Iteration visits each group, with its key and its rows. The groups are in the order in which their keys first
///   appear in the set. The rows of each group are in the order of the set.
/// - `slice` returns the rows of the group that one key identifies.
///
/// Iteration visits only the keys that have rows. If a constraint is written by iterating over a `Grouped`, a key
/// that has no rows in the set gets no constraint, even if another set of the model has that key. For such a key,
/// `slice` returns an empty slice.
///
/// dreamor finds groups through a hash index, but the index decides only which group a row belongs to, never the
/// order of the groups.
///
/// Guide: guide/grouping.md

/// The rows of one group of a `Grouped`, as a view into the set.
///
/// The rows are in the order of the set. A `RowSlice` is a random-access view. It holds the positions of the group's
/// rows in the set and copies no row, so making a slice takes constant time.
///
/// A `RowSlice` refers into the `Grouped` that returned it, and is valid only while that `Grouped` exists. A
/// `RowSlice` is a borrowed range: its iterators refer into the `Grouped`, not into the `RowSlice` object.
template <typename Tup>
class RowSlice : public std::ranges::view_base {
 public:
  /// @hidden - the slice's random-access iterator
  class iterator {
   public:
    using iterator_concept  = std::random_access_iterator_tag;
    using iterator_category = std::random_access_iterator_tag;
    using value_type        = Tup;
    using difference_type   = std::ptrdiff_t;
    using reference         = const Tup&;
    using pointer           = const Tup*;

    iterator() = default;

    [[nodiscard]] reference operator*() const noexcept { return base_[*cursor_]; }

    [[nodiscard]] pointer operator->() const noexcept { return base_ + *cursor_; }

    [[nodiscard]] reference operator[](difference_type n) const noexcept { return base_[cursor_[n]]; }

    iterator& operator++() noexcept {
      ++cursor_;
      return *this;
    }

    iterator operator++(int) noexcept {
      iterator before = *this;
      ++cursor_;
      return before;
    }

    iterator& operator--() noexcept {
      --cursor_;
      return *this;
    }

    iterator operator--(int) noexcept {
      iterator before = *this;
      --cursor_;
      return before;
    }

    iterator& operator+=(difference_type n) noexcept {
      cursor_ += n;
      return *this;
    }

    iterator& operator-=(difference_type n) noexcept {
      cursor_ -= n;
      return *this;
    }

    [[nodiscard]] friend iterator operator+(iterator it, difference_type n) noexcept { return it += n; }

    [[nodiscard]] friend iterator operator+(difference_type n, iterator it) noexcept { return it += n; }

    [[nodiscard]] friend iterator operator-(iterator it, difference_type n) noexcept { return it -= n; }

    [[nodiscard]] friend difference_type operator-(const iterator& a, const iterator& b) noexcept {
      return a.cursor_ - b.cursor_;
    }

    [[nodiscard]] friend bool operator==(const iterator& a, const iterator& b) noexcept {
      return a.cursor_ == b.cursor_;
    }

    [[nodiscard]] friend std::strong_ordering operator<=>(const iterator& a, const iterator& b) noexcept {
      return a.cursor_ <=> b.cursor_;
    }

   private:
    friend class RowSlice;

    iterator(const Tup* base, const std::uint32_t* cursor) noexcept : base_{base}, cursor_{cursor} {}

    const Tup* base_ = nullptr;

    const std::uint32_t* cursor_ = nullptr;
  };

  RowSlice() = default;

  /// Returns an iterator to the first row.
  [[nodiscard]] iterator begin() const noexcept { return {base_, first_}; }

  /// Returns an iterator past the last row.
  [[nodiscard]] iterator end() const noexcept { return {base_, first_ + count_}; }

  /// Returns the number of rows.
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  /// Checks whether the slice has no rows.
  ///
  /// Returns: True if the slice has no rows; false otherwise.
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

  /// Returns the row at position `i` of the slice.
  ///
  /// Positions count from 0. `i` is not checked: if `i` is not less than `size()`, the behavior is undefined.
  [[nodiscard]] const Tup& operator[](std::size_t i) const noexcept { return base_[first_[i]]; }

  /// Returns the first row.
  ///
  /// If the slice is empty, the behavior is undefined.
  [[nodiscard]] const Tup& front() const noexcept { return (*this)[0]; }

  /// Returns the last row.
  ///
  /// If the slice is empty, the behavior is undefined.
  [[nodiscard]] const Tup& back() const noexcept { return (*this)[count_ - 1]; }

 private:
  template <typename, typename...>
  friend class Grouped;
  friend struct internal::SliceAccess;

  RowSlice(const Tup* base, const std::uint32_t* first, std::size_t count) noexcept
      : base_{base}, first_{first}, count_{count} {}

  const Tup* base_ = nullptr;

  const std::uint32_t* first_ = nullptr;

  std::size_t count_ = 0;
};

/// One group of a `Grouped`: its key and its rows.
///
/// A structured binding of a `Group` binds one name to each column of the key, in order, and a last name to the
/// rows.
template <typename Tup, typename... Columns>
struct Group {
  /// The values of the key columns, which every row of the group has.
  std::tuple<Columns...> key;
  /// The rows of the group, in the order of the set.
  ///
  /// The rows are valid while the `Grouped` exists.
  RowSlice<Tup> rows;

  /// Returns the column of the key at position `I`, or the rows if `I` is the number of key columns.
  ///
  /// A structured binding of a `Group` calls `get`.
  ///
  /// Takes: `I` must not be greater than the number of key columns.
  template <std::size_t I>
  [[nodiscard]] const auto& get() const noexcept {
    static_assert(I <= sizeof...(Columns), "Group: index past the rows.");
    if constexpr (I < sizeof...(Columns)) {
      return std::get<I>(key);
    } else {
      return rows;
    }
  }
};

/// The rows of a set, divided into groups by the values of the columns `Columns...`.
///
/// A `Grouped` is a range of `Group`s, one for each key, in the order in which the keys first appear in the set.
/// `slice` returns the rows of the group that a key identifies, and `at` returns the group at a position.
///
/// A `Grouped` holds a copy of the set, which shares the rows. For each row, the `Grouped` stores the row's position
/// in the set as a 4-byte number, not a copy of the row.
///
/// A `Grouped` that is an rvalue can be iterated, because a range-based `for` loop keeps it until the loop ends. A
/// call of `slice` or `at` on an rvalue `Grouped` does not compile. These functions return views into the `Grouped`,
/// and an rvalue `Grouped` is destroyed at the end of the statement.
template <typename Set, typename... Columns>
class Grouped {
  using Tup = Set::value_type;

 public:
  using key_type = std::tuple<Columns...>;
  /// @hidden - its groups' type, a Group
  using Group = ::dreamor::Group<Tup, Columns...>;

  /// @hidden - a forward iterator over the groups, each step making a Group
  class GroupIterator {
   public:
    // A step makes a Group, so *it is a value, not a reference. C++20 calls that a forward iterator; C++17's forward
    // iterator returns a reference, so to C++17's algorithms it is an input iterator, as std::views::transform's is.
    using iterator_concept  = std::forward_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type        = Group;
    using difference_type   = std::ptrdiff_t;
    using reference         = Group;

    GroupIterator() = default;

    GroupIterator(const Grouped* owner, std::size_t group) noexcept : owner_{owner}, group_{group} {}

    [[nodiscard]] reference operator*() const { return Group{owner_->keys_[group_], owner_->sliceOf(group_)}; }

    GroupIterator& operator++() noexcept {
      ++group_;
      return *this;
    }

    GroupIterator operator++(int) noexcept {
      GroupIterator before = *this;
      ++*this;
      return before;
    }

    [[nodiscard]] friend bool operator==(const GroupIterator& a, const GroupIterator& b) noexcept {
      return a.group_ == b.group_;
    }

   private:
    const Grouped* owner_ = nullptr;
    std::size_t group_    = 0;
  };

  /// Constructs the groups of the rows of `set` by the columns `Columns...`.
  ///
  /// `IndexSet::groupBy` calls this constructor.
  ///
  /// Throws: `std::length_error` if `set` has more than 2^32 - 2 rows.
  /// Cost: `O(|set|)`.
  explicit Grouped(Set set) : set_{std::move(set)} {
    // The constructor sorts the rows by group number with a counting sort, which is O(n) and stable. The result is in
    // compressed sparse rows (CSR): group g spans rows_[offsets_[g]] to rows_[offsets_[g + 1] - 1].
    //
    // If the key's columns were bound outermost when the set was built, the rows of one key come in a run. This is
    // the usual case when a set is grouped by its first column. So the constructor compares a row's key with the
    // previous row's group before it looks the key up in the hash index. While every change of key opens a new
    // group, the groups are the runs: rows_ keeps the set's own order, and nothing is sorted. The row-to-group array
    // that the sort needs is made only if a key appears again after another key.
    const auto& rows = internal::SetAccess::rows(set_);
    if (rows.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error("groupBy: more than 2^32 - 2 rows.");
    }
    std::vector<std::uint32_t> group_of;  // Stays empty while the groups are runs.
    bool runs            = true;
    std::size_t previous = kNoGroup;  // The group of the previous row.
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const key_type key = keyOf(rows[i]);
      if (previous == kNoGroup || !(keys_[previous] == key)) {
        previous = index_.insert(key, keys_.size(), keysAt());
        if (previous == kNoGroup) {
          previous = keys_.size();
          keys_.push_back(key);
          offsets_.push_back(0);  // Holds the group's row count until the prefix sum below.
        } else if (runs) {
          runs     = false;
          group_of = groupsOfRuns(rows.size());  // The groups of the rows so far, which came in runs.
        }
      }
      if (!runs) { group_of[i] = static_cast<std::uint32_t>(previous); }
      ++offsets_[previous];
    }
    offsets_.push_back(0);  // A sentinel, so that offsets_ has K + 1 elements for K groups.

    std::size_t running = 0;
    for (std::size_t group = 0; group < keys_.size(); ++group) {
      const std::size_t count = offsets_[group];

      offsets_[group] = running;
      running += count;
    }
    offsets_[keys_.size()] = running;

    // This is assign, not resize, although both fill an empty vector. If a value column's set is inlined here, GCC 13
    // at -O3 reads resize as a store through a null pointer and warns (-Wnull-dereference, inside std::_Construct).
    rows_.assign(rows.size(), std::uint32_t{0});
    if (runs) {
      std::iota(rows_.begin(), rows_.end(), std::uint32_t{0});
      return;
    }
    // offsets_ also serves as the moving cursor of each group, so no second array is needed.
    for (std::size_t i = 0; i < rows.size(); ++i) {
      rows_[offsets_[group_of[i]]++] = static_cast<std::uint32_t>(i);
    }

    // Each offsets_[g] now holds the end of group g, which is the start of group g + 1. The loop below shifts them.
    for (std::size_t group = keys_.size(); group > 0; --group) {
      offsets_[group] = offsets_[group - 1];
    }
    offsets_[0] = 0;
  }

  /// Returns the number of groups.
  [[nodiscard]] std::size_t size() const noexcept { return keys_.size(); }

  /// Checks whether the `Grouped` has no groups.
  ///
  /// A `Grouped` has no groups exactly when the set has no rows.
  ///
  /// Returns: True if the `Grouped` has no groups; false otherwise.
  [[nodiscard]] bool empty() const noexcept { return keys_.empty(); }

  /// Returns the group at position `group`, with its key and its rows.
  ///
  /// Positions count from 0, in the order in which iteration visits the groups.
  ///
  /// Throws: `std::out_of_range` if `group` is greater than or equal to `size()`.
  [[nodiscard]] Group at(std::size_t group) const& {
    // The position is checked: for an empty set, offsets_ is {0}, so an unchecked at(0) would read offsets_[1].
    if (group >= keys_.size()) { throw std::out_of_range("Grouped::at: group index out of range."); }
    return Group{keys_[group], sliceOf(group)};
  }

  /// @hidden - exists only to reject a call on a temporary Grouped with a message
  template <typename Unused = void>
  [[nodiscard]] Group at(std::size_t /*group*/) const&& {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NAME_THE_GROUPED_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a column in place of a position with a message
  template <typename Column>
    requires internal::column<Column>
  [[nodiscard]] Group at(const Column& /*key*/) const {
    // A column of integers converts to its number. Without this overload, at() would read the number as a position.
    static_assert(internal::always_false<Column>,
                  "at() takes a group's position, 0 to size() - 1, not a column, which would be read as a "
                  "position. For a key's group, call slice(key).");
    std::terminate();
  }

  /// Returns the rows of the group that `key` identifies.
  ///
  /// Takes: `key` must be a key of the columns `Columns...`, as the Keys section describes.
  /// Returns: An empty slice if no row of the set has the key.
  template <typename... Args>
  [[nodiscard]] RowSlice<Tup> slice(const Args&... key) const& {
    if constexpr (internal::checkGroupKey<key_type, Args...>()) {
      const auto found_key    = internal::keyOf<key_type>(key...);
      const std::size_t group = found_key ? index_.find(*found_key, keysAt()) : kNoGroup;
      return (group == kNoGroup) ? RowSlice<Tup>{} : sliceOf(group);
    } else {
      return RowSlice<Tup>{};
    }
  }

  /// @hidden - exists only to reject a call on a temporary Grouped with a message
  template <typename... Args>
  RowSlice<Tup> slice(const Args&... /*key*/) const&& {
    static_assert(internal::always_false<Args...>, DREAMOR_INTERNAL_NAME_THE_GROUPED_MESSAGE);
    std::terminate();
  }

  /// Checks whether a group has the key `key`.
  ///
  /// Takes: `key` must be a key of the columns `Columns...`, as the Keys section describes.
  /// Returns: True if some row of the set has the key; false otherwise.
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... key) const {
    if constexpr (internal::checkGroupKey<key_type, Args...>()) {
      const auto found_key = internal::keyOf<key_type>(key...);
      return found_key && index_.find(*found_key, keysAt()) != kNoGroup;
    } else {
      return false;
    }
  }

  /// @hidden - exists only to reject a key in braces with a message
  template <typename Unused = void>
  [[nodiscard]] RowSlice<Tup> slice(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key in braces with a message
  template <typename Unused = void>
  [[nodiscard]] bool contains(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  // begin() and end() are deleted for rvalues, as at() and slice() reject an rvalue. A range-based for loop binds the
  // range to a name, so it never calls them on an rvalue.

  /// Returns an iterator to the first group.
  ///
  /// The groups are in the order in which their keys first appear in the set. Dereferencing the iterator returns a
  /// `Group` by value.
  [[nodiscard]] GroupIterator begin() const& noexcept { return {this, 0}; }

  /// Returns an iterator past the last group.
  [[nodiscard]] GroupIterator end() const& noexcept { return {this, keys_.size()}; }

  [[nodiscard]] GroupIterator begin() const&& = delete;
  [[nodiscard]] GroupIterator end() const&&   = delete;

  /// Returns a new set with the rows of the set, laid out group by group.
  ///
  /// Returns: The groups are in the order in which their keys first appear in the set. The rows of each group are in
  /// the order of the set.
  [[nodiscard]] Set regrouped() const {
    const auto& rows = internal::SetAccess::rows(set_);
    internal::Rows<Tup> out;
    out.reserve(rows_.size());
    for (const std::uint32_t i : rows_) {
      out.push_back(rows[i]);
    }
    return internal::SetAccess::make(std::move(out));
  }

 private:
  static constexpr std::size_t kNoGroup = ::dreamor::internal::FlatIndex<key_type>::kNotFound;

  [[nodiscard]] static key_type keyOf(const Tup& tuple) { return key_type{std::get<Columns>(tuple)...}; }

  // Returns the row-to-group array of rows that came in runs, one group after another. offsets_ still holds the row
  // counts here, so the first offsets_[0] rows are group 0, the next offsets_[1] rows are group 1, and so on.
  [[nodiscard]] std::vector<std::uint32_t> groupsOfRuns(std::size_t size) const {
    std::vector<std::uint32_t> group_of(size);
    std::size_t row = 0;
    for (std::size_t group = 0; group < keys_.size(); ++group) {
      for (std::size_t n = 0; n < offsets_[group]; ++n) {
        group_of[row++] = static_cast<std::uint32_t>(group);
      }
    }
    return group_of;
  }

  // The entries of index_ are group numbers. Their keys are in keys_.
  [[nodiscard]] auto keysAt() const {
    return [this](std::size_t group) -> const key_type& { return keys_[group]; };
  }

  // The group number is not checked, because every caller takes it from index_ or from a walk over keys_.
  [[nodiscard]] RowSlice<Tup> sliceOf(std::size_t group) const {
    return RowSlice<Tup>{
        internal::SetAccess::rows(set_).data(),
        rows_.data() + offsets_[group],
        offsets_[group + 1] - offsets_[group],
    };
  }

  Set set_;                          // Shares the rows with the caller's set, so no row is copied.
  std::vector<std::uint32_t> rows_;  // Row numbers of set_, group by group.
  std::vector<key_type> keys_;
  std::vector<std::size_t> offsets_;                // K + 1 CSR offsets into rows_.
  ::dreamor::internal::FlatIndex<key_type> index_;  // From a key to its group number, over keys_.
};

template <typename... Columns>
template <typename... By>
Grouped<IndexSet<Columns...>, By...> IndexSet<Columns...>::groupBy() const {
  static_assert(
      sizeof...(By) > 0,
      "groupBy<Columns...>() names the key columns: S.groupBy<cMachine>(), or by two, S.groupBy<cMachine, cPeriod>().");
  internal::checkColumns<Tup, By...>();
  return Grouped<IndexSet, By...>{*this};
}

}  // namespace dreamor

// The iterators of a slice point into the set and the Grouped, not into the slice object, so a slice is borrowed.
template <typename Tup>
inline constexpr bool std::ranges::enable_borrowed_range<dreamor::RowSlice<Tup>> = true;

// The tuple protocol of a Group, for a structured binding: the key's columns, then the rows. Group::get reads them.
template <typename Tup, typename... Columns>
struct std::tuple_size<dreamor::Group<Tup, Columns...>> : std::integral_constant<std::size_t, sizeof...(Columns) + 1> {
};

template <std::size_t I, typename Tup, typename... Columns>
struct std::tuple_element<I, dreamor::Group<Tup, Columns...>> {
  using type = std::conditional_t<(I < sizeof...(Columns)),
                                  std::tuple_element_t<(I < sizeof...(Columns) ? I : 0), std::tuple<Columns...>>,
                                  dreamor::RowSlice<Tup>>;
};
