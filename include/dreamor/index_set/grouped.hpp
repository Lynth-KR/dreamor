// SPDX-License-Identifier: Apache-2.0
// File: grouped.hpp
// Groups an index set by an arbitrary subset of its columns, laying each group out
// contiguously.
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"
#include "dreamor/index_set/column.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"

namespace dreamor {

// One group: rows of the grouped set, in the set's order. A random-access view - size(),
// empty(), rows[i], front(), back(), range-for - over row numbers into the set, so it
// costs nothing to hand out and copies no row. Valid while the Grouped it came from lives.
template <typename Tup>
class RowSlice : public std::ranges::view_base {
 public:
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

  [[nodiscard]] iterator begin() const noexcept { return {base_, first_}; }
  [[nodiscard]] iterator end() const noexcept { return {base_, first_ + count_}; }
  [[nodiscard]] std::size_t size() const noexcept { return count_; }
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
  [[nodiscard]] const Tup& operator[](std::size_t i) const noexcept { return base_[first_[i]]; }
  [[nodiscard]] const Tup& front() const noexcept { return (*this)[0]; }
  [[nodiscard]] const Tup& back() const noexcept { return (*this)[count_ - 1]; }

 private:
  template <typename, typename...>
  friend class Grouped;
  RowSlice(const Tup* base, const std::uint32_t* first, std::size_t count) noexcept
      : base_{base}, first_{first}, count_{count} {}

  const Tup* base_ = nullptr;

  const std::uint32_t* first_ = nullptr;

  std::size_t count_ = 0;
};

// One group of a Grouped: its key and its rows. It destructures flat - the key's columns
// first, the rows last - so a loop names what it reads:
//
//     for (const auto& [machine, period, rows] : sPRODUCE.groupBy<MachineCol, PeriodCol>()) ...
//
// `key` (the whole tuple) and `tuples` are there too. The rows borrow from the Grouped,
// like slice()'s.
template <typename Tup, typename... Columns>
struct Group {
  std::tuple<Columns...> key;
  RowSlice<Tup> tuples;

  // Tuple protocol for structured bindings: get<0..N-1> are the key's columns, get<N> the rows.
  template <std::size_t I>
  [[nodiscard]] const auto& get() const noexcept {
    static_assert(I <= sizeof...(Columns), "Group: index past the rows.");
    if constexpr (I < sizeof...(Columns)) {
      return std::get<I>(key);
    } else {
      return tuples;
    }
  }
};

// Covers the two things constraint code needs:
//
//   1) walk every group          ∀c: Σ_{(a,b) : (a,b,c) ∈ S} x_abc
//        for (const auto& [c, tuples] : byC) ...
//
//   2) slice by a specific key   ∀(i,j) ∈ ARCS: Σ_{k : (i,k) ∈ LINKS} ...
//        for (const auto& [a, b] : byFirst.slice(i)) ...
//
// Only (2) needs the index; driving repeated point slices from another set is
// |driver| x |set| without one. Walking all groups is a single pass either way.
//
// A Grouped is the range of its groups. Walking it needs no name: range-for keeps the
// range expression alive for the whole loop, so `for (... : S.groupBy<C>())` is safe. What
// borrows beyond one expression - slice(), at(), keyAt() - needs the Grouped named first.
//
// Walking yields only the groups that exist. When the driver set has keys this one does
// not - a machine with no batches, a batch with no items - a constraint family written by
// walking the groups skips them silently; slice() hands back an empty slice instead.
//
// Group order is first appearance of the key, order within a group is the set's original
// order; hashing decides membership only, never layout.
//
// The set is shared, not copied: the groups are row numbers into it (4 bytes a row), laid
// out group by group.
template <typename Tup, typename... Columns>
class Grouped {
 public:
  using Key   = std::tuple<Columns...>;
  using Group = ::dreamor::Group<Tup, Columns...>;

  // Forward iterator over the groups, first appearance first; each step makes a Group.
  class GroupIterator {
   public:
    using iterator_concept  = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
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

  // Counting sort on the group index: O(n), stable, CSR-shaped - group g spans
  // [offsets_[g], offsets_[g + 1]) of rows_.
  explicit Grouped(const IndexSet<Tup>& set) : set_{set} {
    const auto& rows = internal::SetAccess::rows(set_);
    if (rows.size() >= std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error("groupBy: more than 2^32 - 2 rows.");
    }
    std::vector<std::uint32_t> group_of(rows.size());
    std::size_t previous = kNoGroup;  // The previous row's group.
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const Key key = keyOf(rows[i]);
      // A set comes in runs of one key whenever the key's columns were bound outermost when it
      // was generated - grouping by a leading column, typically - so the previous row's group
      // is tried before the hash.
      if (previous == kNoGroup || !(keys_[previous] == key)) {
        previous = index_.insert(key, keys_.size(), keysAt());
        if (previous == kNoGroup) {
          previous = keys_.size();
          keys_.push_back(key);
          offsets_.push_back(0);  // Holds this group's COUNT until the prefix sum below.
        }
      }
      group_of[i] = static_cast<std::uint32_t>(previous);
      ++offsets_[previous];
    }
    offsets_.push_back(0);  // Sentinel: offsets_ ends up size K + 1.

    std::size_t running = 0;
    for (std::size_t group = 0; group < keys_.size(); ++group) {
      const std::size_t count = offsets_[group];

      offsets_[group] = running;
      running += count;
    }
    offsets_[keys_.size()] = running;

    // offsets_ doubles as the moving cursor, so no second array is needed.
    rows_.resize(rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
      rows_[offsets_[group_of[i]]++] = static_cast<std::uint32_t>(i);
    }

    // Each offsets_[g] now holds the END of group g, which is the START of g + 1.
    for (std::size_t group = keys_.size(); group > 0; --group) {
      offsets_[group] = offsets_[group - 1];
    }
    offsets_[0] = 0;
  }

  [[nodiscard]] std::size_t size() const noexcept {  // number of groups
    return keys_.size();
  }
  [[nodiscard]] bool empty() const noexcept { return keys_.empty(); }

  // The accessors below borrow from this object's storage, so on an rvalue they stop with a
  // sentence: the object has to be named first, `const auto byMachine = S.groupBy<MachineCol>();`.
  [[nodiscard]] const Key& keyAt(std::size_t group) const& { return keys_.at(group); }
  template <typename Unused = void>
  [[nodiscard]] const Key& keyAt(std::size_t /*group*/) const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Grouped first - const auto byMachine = S.groupBy<MachineCol>(); - slice(), at() and keyAt() hand out views into it, which would dangle once a temporary is gone. Walking it needs no name: for (... : S.groupBy<MachineCol>()) is fine.");
    std::terminate();
  }

  // Checked: for an empty set offsets_ is just {0}, so an unchecked at(0) would read
  // offsets_[1] out of bounds.
  [[nodiscard]] RowSlice<Tup> at(std::size_t group) const& {
    if (group >= keys_.size()) {
      throw std::out_of_range("Grouped::at: group index out of range.");
    }
    return sliceOf(group);
  }
  template <typename Unused = void>
  [[nodiscard]] RowSlice<Tup> at(std::size_t /*group*/) const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Grouped first - const auto byMachine = S.groupBy<MachineCol>(); - slice(), at() and keyAt() hand out views into it, which would dangle once a temporary is gone. Walking it needs no name: for (... : S.groupBy<MachineCol>()) is fine.");
    std::terminate();
  }

  // The rows of a key - byItemPeriod.slice(i, t), or slice(key) with a key from keyAt() - or an
  // empty slice when the key is absent, so callers need no membership check first. The key's
  // columns themselves, in order (see key_args.hpp).
  template <typename... Args>
  [[nodiscard]] RowSlice<Tup> slice(const Args&... key) const& {
    if constexpr (internal::checkGroupKey<Key, Args...>()) {
      const std::size_t group = index_.find(internal::keyOf<Key>(key...), keysAt());
      return (group == kNoGroup) ? RowSlice<Tup>{} : sliceOf(group);
    } else {
      return RowSlice<Tup>{};
    }
  }
  template <typename... Args>
  RowSlice<Tup> slice(const Args&... /*key*/) const&& {
    static_assert(
        internal::always_false<Args...>,
        "Name the Grouped first - const auto byMachine = S.groupBy<MachineCol>(); - slice(), at() and keyAt() hand out views into it, which would dangle once a temporary is gone. Walking it needs no name: for (... : S.groupBy<MachineCol>()) is fine.");
    std::terminate();
  }
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... key) const {
    if constexpr (internal::checkGroupKey<Key, Args...>()) {
      return index_.find(internal::keyOf<Key>(key...), keysAt()) != kNoGroup;
    } else {
      return false;
    }
  }
  template <typename... Args>
  [[nodiscard]] std::size_t sliceSize(const Args&... key) const {
    if constexpr (internal::checkGroupKey<Key, Args...>()) {
      const std::size_t group = index_.find(internal::keyOf<Key>(key...), keysAt());
      return (group == kNoGroup) ? 0 : offsets_[group + 1] - offsets_[group];
    } else {
      return 0;
    }
  }
  // slice({i, t}) and the like: braces are not a key.
  template <typename Unused = void>
  [[nodiscard]] RowSlice<Tup> slice(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] bool contains(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] std::size_t sliceSize(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  // The groups, as a range: `for (const auto& [machine, period, rows] : byMachinePeriod)`.
  // Deleted on rvalues like the accessors above; range-for never calls them on one, since it
  // binds the range expression to a name first.
  [[nodiscard]] GroupIterator begin() const& noexcept { return {this, 0}; }
  [[nodiscard]] GroupIterator end() const& noexcept { return {this, keys_.size()}; }
  [[nodiscard]] GroupIterator begin() const&& = delete;
  [[nodiscard]] GroupIterator end() const&&   = delete;

  // The set with its groups made contiguous, as a set of its own.
  [[nodiscard]] IndexSet<Tup> regrouped() const {
    const auto& rows = internal::SetAccess::rows(set_);
    std::vector<Tup> out;
    out.reserve(rows_.size());
    for (const std::uint32_t i : rows_) {
      out.push_back(rows[i]);
    }
    return internal::SetAccess::make(std::move(out));
  }

 private:
  static constexpr std::size_t kNoGroup = ::dreamor::general::FlatIndex<Key>::kNotFound;

  [[nodiscard]] static Key keyOf(const Tup& tuple) { return Key{std::get<Columns>(tuple)...}; }

  // The index's entries are group numbers; their keys live in keys_.
  [[nodiscard]] auto keysAt() const {
    return [this](std::size_t group) -> const Key& { return keys_[group]; };
  }

  // Unchecked: the group index provably comes from index_ or from walking keys_.
  [[nodiscard]] RowSlice<Tup> sliceOf(std::size_t group) const {
    return RowSlice<Tup>{
        internal::SetAccess::rows(set_).data(),
        rows_.data() + offsets_[group],
        offsets_[group + 1] - offsets_[group],
    };
  }

  IndexSet<Tup> set_;                // Shared with the caller's set: no row is copied.
  std::vector<std::uint32_t> rows_;  // Row numbers of set_, group by group.
  std::vector<Key> keys_;
  std::vector<std::size_t> offsets_;          // Size K + 1; CSR row pointers into rows_.
  ::dreamor::general::FlatIndex<Key> index_;  // Key -> group number, over keys_.
};

// S.groupBy<ItemCol>() groups by the item column; S.groupBy<MachineCol, OrderCol>() by two.
template <typename Tup>
template <typename... Columns>
Grouped<Tup, Columns...> IndexSet<Tup>::groupBy() const {
  static_assert(sizeof...(Columns) > 0, "groupBy needs at least one column.");
  internal::checkColumns<Tup, Columns...>();
  return Grouped<Tup, Columns...>{*this};
}

}  // namespace dreamor

// A slice's iterators point into the set and the Grouped, not into the slice object.
template <typename Tup>
inline constexpr bool std::ranges::enable_borrowed_range<dreamor::RowSlice<Tup>> = true;

// Tuple protocol of a Group (see Group::get): the key's columns, then the rows.
template <typename Tup, typename... Columns>
struct std::tuple_size<dreamor::Group<Tup, Columns...>> : std::integral_constant<std::size_t, sizeof...(Columns) + 1> {
};
template <std::size_t I, typename Tup, typename... Columns>
struct std::tuple_element<I, dreamor::Group<Tup, Columns...>> {
  using type = std::conditional_t<(I < sizeof...(Columns)),
                                  std::tuple_element_t<(I < sizeof...(Columns) ? I : 0), std::tuple<Columns...>>,
                                  dreamor::RowSlice<Tup>>;
};
