// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Ordered: the position of each row of a set in the set's order, over the whole set or within each group of rows that
// have equal values in some columns.
#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/base/flat_index.hpp"
#include "dreamor/internal/base/unreachable.hpp"
#include "dreamor/internal/index_set/algebra.hpp"
#include "dreamor/internal/index_set/grouped.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/logger.hpp"

// The message for a lookup on an rvalue Ordered that would return a reference or a pointer to a row.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE                                                                      \
  "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, " \
  "which would dangle once a temporary is gone."
// The message for at(), first() and last() on an Ordered within groups, where they would refer to positions of the
// whole set.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_POSITION_WITHIN_MESSAGE                                                                      \
  "orderedWithin<Columns...>() orders within each group: at(), first() and last() name positions of the whole set. "  \
  "Use first(m) and last(m) for a group's ends, positionOf(), isFirst() and isLast() within a group, or S.ordered() " \
  "for the whole set."

namespace dreamor {

/// @section Ordered
///
/// The parts of this section read the order of a set's rows. They return the position of a row, the first and the
/// last row, and the rows a given number of positions before or after a row. The order is the order in which the set
/// holds its rows.
///
/// `IndexSet::ordered()` returns an `Ordered` over the whole set. `IndexSet::orderedWithin<Within...>()` returns an
/// `Ordered` that orders each group of rows apart. A group is the rows that have equal values in the columns
/// `Within...`.
///
/// Positions count from 0. A lookup in an `Ordered` takes a key, as the Keys section describes.
///
/// Guide: guide/ordered-sets.md

/// The positions of the rows of a set, in the order of the set.
///
/// An `Ordered` has one of two forms:
///
/// - Without `Within...`, the `Ordered` is over the whole set. Positions count from the first row of the set.
///   `IndexSet::ordered()` returns this form.
/// - With `Within...`, the `Ordered` is within groups. Positions count from the first row of each group.
///   `IndexSet::orderedWithin<Within...>()` returns this form.
///
/// An `Ordered` within groups holds the rows of the set laid out group by group. The groups come in the order in
/// which their keys first appear in the set. The rows of each group keep the order of the set. A walk from a row with
/// `next`, `prev`, `nextOf` or `prevOf` stops at the ends of the row's group, and `nextCyclic` and `prevCyclic` wrap
/// around them. Within groups, `at` does not compile, and neither do `first()` and `last()` without a key, because
/// they refer to positions of the whole set.
///
/// The lookups that return a reference or a pointer to a row do not compile on an rvalue `Ordered`, because the row
/// may be one that the `Ordered` holds. These lookups are `members`, `at`, `first`, `last`, `next`, `prev`, `nextOf`,
/// `prevOf`, `nextCyclic` and `prevCyclic`. Call them on an lvalue `Ordered`.
template <typename Set, typename... Within>
class Ordered {
  using Tup = Set::value_type;

  static constexpr bool kWithinGroups = sizeof...(Within) > 0;
  using GroupKey                      = std::tuple<Within...>;

  struct NoGroups {};

  // Within groups, the index from a group's key to the group's number, for first(key) and last(key).
  using GroupIndex = std::conditional_t<kWithinGroups, ::dreamor::internal::FlatIndex<GroupKey>, NoGroups>;

 public:
  /// Constructs an `Ordered` over the rows of `set`.
  ///
  /// `set.ordered()` and `set.orderedWithin<Within...>()` call this constructor. An `Ordered` over the whole set
  /// shares the rows of `set` and does not copy them. An `Ordered` within groups holds a copy of the rows, laid out
  /// group by group.
  ///
  /// Cost: `O(|set|)`. The constructor builds an index from each row to its position.
  explicit Ordered(const Set& set) {
    // insertAll stops at the first key that repeats, so its result is ignored here: the rows of a set never repeat, and
    // neither do the keys of its groups.
    if constexpr (kWithinGroups) {
      const Grouped<Set, Within...> groups{set};
      members_ = groups.regrouped();
      group_starts_.reserve(groups.size() + 1);
      group_of_.reserve(members_.size());
      group_starts_.push_back(0);
      for (const auto& group : groups) {
        group_of_.insert(group_of_.end(), group.rows.size(), static_cast<std::uint32_t>(group_starts_.size() - 1));
        group_starts_.push_back(group_starts_.back() + group.rows.size());
      }
      group_keys_.reserve(groups.size());
      for (std::size_t group = 0; group < groups.size(); ++group) {
        group_keys_.push_back(groups.at(group).key);
      }
      group_index_ = GroupIndex{group_keys_.size()};
      static_cast<void>(group_index_.insertAll(0, group_keys_.size(), groupKeysAt()));
    } else {
      members_ = set;
    }
    positions_ = ::dreamor::internal::FlatIndex<Tup>{members_.size()};
    static_cast<void>(positions_.insertAll(0, rows().size(), rowsAt()));
  }

  /// Returns the number of rows.
  [[nodiscard]] std::size_t size() const noexcept { return members_.size(); }

  /// Checks whether the `Ordered` holds no rows.
  ///
  /// Returns: True if the `Ordered` holds no rows; false otherwise.
  [[nodiscard]] bool empty() const noexcept { return members_.size() == 0; }

  // Each lookup that returns a reference or a pointer has an overload for an rvalue Ordered that rejects the call. The
  // result would refer into members_, and within groups the rows exist only there, so they end with the rvalue.

  /// Returns the rows that the positions count, as a set.
  ///
  /// For an `Ordered` over the whole set, the returned set shares the rows of the set that the `Ordered` was made
  /// from, in the same order. For an `Ordered` within groups, the returned set holds the same rows, laid out group by
  /// group.
  ///
  /// Takes: The `Ordered` must be an lvalue.
  [[nodiscard]] const Set& members() const& noexcept { return members_; }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename Unused = void>
  [[nodiscard]] const Set& members() const&& {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// Returns the row at `position`.
  ///
  /// To find the position of a row, use `positionOf`.
  ///
  /// Takes: `position` must be a position, not a column, even if the column holds integers. The `Ordered` must be an
  /// lvalue, and it must be over the whole set.
  /// Throws: `std::out_of_range` if `position` is `size()` or greater.
  [[nodiscard]] const Tup& at(std::size_t position) const& {
    static_assert(!kWithinGroups, DREAMOR_INTERNAL_POSITION_WITHIN_MESSAGE);
    return rows().at(position);
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup& at(std::size_t /*position*/) const&& {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  // Without this overload, a column of integers would convert to its value, and at() would read the value as a
  // position.

  /// @hidden - exists only to reject a column in place of a position with a message
  template <typename Column>
    requires internal::column<Column>
  [[nodiscard]] const Tup& at(const Column& /*element*/) const {
    static_assert(internal::always_false<Column>,
                  "at() takes a position, 0 to size() - 1, not a column, which would be read as a position. For an "
                  "element's position, call positionOf(element).");
    std::terminate();
  }

  /// Returns the first row of the set.
  ///
  /// For an `Ordered` within groups, `first` takes the key of a group.
  ///
  /// Takes: The `Ordered` must be an lvalue, and it must be over the whole set.
  /// Contract: ordered_set_has_row
  [[nodiscard]] const Tup& first() const& {
    static_assert(!kWithinGroups, DREAMOR_INTERNAL_POSITION_WITHIN_MESSAGE);
    return setEnd<true>("first");
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup& first() const&& {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// Returns the last row of the set.
  ///
  /// For an `Ordered` within groups, `last` takes the key of a group.
  ///
  /// Takes: The `Ordered` must be an lvalue, and it must be over the whole set.
  /// Contract: ordered_set_has_row
  [[nodiscard]] const Tup& last() const& {
    static_assert(!kWithinGroups, DREAMOR_INTERNAL_POSITION_WITHIN_MESSAGE);
    return setEnd<false>("last");
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup& last() const&& {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// @overload
  /// Returns the first row of the group that the key identifies.
  ///
  /// A group is the rows that have equal values in the columns `Within...`. The rows of a group keep the order of
  /// the set.
  ///
  /// Takes: `group` and `rest` must form a key of the columns `Within...`, as the Keys section describes. The
  /// `Ordered` must be an lvalue, and it must be within groups.
  /// Contract: ordered_group_has_row
  template <typename Arg, typename... Args>
  [[nodiscard]] const Tup& first(const Arg& group, const Args&... rest) const& {
    return groupEnd<true>("first", group, rest...);
  }

  /// @overload
  /// Returns the last row of the group that the key identifies.
  ///
  /// A group is the rows that have equal values in the columns `Within...`. The rows of a group keep the order of
  /// the set.
  ///
  /// Takes: `group` and `rest` must form a key of the columns `Within...`, as the Keys section describes. The
  /// `Ordered` must be an lvalue, and it must be within groups.
  /// Contract: ordered_group_has_row
  template <typename Arg, typename... Args>
  [[nodiscard]] const Tup& last(const Arg& group, const Args&... rest) const& {
    return groupEnd<false>("last", group, rest...);
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename Arg, typename... Args>
  const Tup& first(const Arg& /*group*/, const Args&... /*rest*/) const&& {
    static_assert(internal::always_false<Arg>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename Arg, typename... Args>
  const Tup& last(const Arg& /*group*/, const Args&... /*rest*/) const&& {
    static_assert(internal::always_false<Arg>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// Returns the position of the row that the key identifies.
  ///
  /// For an `Ordered` within groups, the position counts from the first row of the row's group.
  ///
  /// Takes: `element` must be a key of the set, as the Keys section describes.
  /// Returns: An empty `std::optional` if no row of the set has the key.
  template <typename... Args>
  [[nodiscard]] std::optional<std::size_t> positionOf(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      const auto position = positionInMembers(internal::keyOf<Tup>(element...));
      if (!position.has_value()) { return std::nullopt; }
      return *position - spanOf(*position).first;
    } else {
      return std::nullopt;
    }
  }

  /// Returns a pointer to the row a given number of steps after the row that the key identifies.
  ///
  /// For an `Ordered` within groups, the walk stays in the group of the row that the key identifies.
  ///
  /// `next` returns a null pointer in these cases:
  ///
  /// - No row of the set has the key.
  /// - The walk ends after the last row or before the first row of the set, or of the group for an `Ordered` within
  ///   groups.
  ///
  /// `nextOf` walks in the same way and returns the row itself instead of a pointer. `nextCyclic` wraps around the
  /// ends instead of returning a null pointer.
  ///
  /// Takes: `element_and_steps` must be a key of the set, as the Keys section describes, and may be followed by a
  /// step count. A step count must be an integer other than `bool`, or a column of integers. If the step count is
  /// left out, it is 1. A negative step count walks toward the first row. The `Ordered` must be an lvalue.
  /// Returns: A pointer to a row that the `Ordered` holds. The pointer is valid while the `Ordered` exists.
  template <typename... Args>
  [[nodiscard]] const Tup* next(const Args&... element_and_steps) const& {
    return walk<false, false>(element_and_steps...);
  }

  /// Returns a pointer to the row a given number of steps before the row that the key identifies.
  ///
  /// `prev` walks in the direction opposite to `next`, and otherwise behaves as `next` does. For an `Ordered` within
  /// groups, the walk stays in the group of the row that the key identifies.
  ///
  /// `prev` returns a null pointer in these cases:
  ///
  /// - No row of the set has the key.
  /// - The walk ends before the first row or after the last row of the set, or of the group for an `Ordered` within
  ///   groups.
  ///
  /// Takes: `element_and_steps` must be a key of the set, as the Keys section describes, and may be followed by a
  /// step count. A step count must be an integer other than `bool`, or a column of integers. If the step count is
  /// left out, it is 1. A negative step count walks toward the last row. The `Ordered` must be an lvalue.
  /// Returns: A pointer to a row that the `Ordered` holds. The pointer is valid while the `Ordered` exists.
  template <typename... Args>
  [[nodiscard]] const Tup* prev(const Args&... element_and_steps) const& {
    return walk<false, true>(element_and_steps...);
  }

  /// Returns the row a given number of steps after the row that the key identifies.
  ///
  /// `nextOf` walks in the same way as `next`, but returns the row itself instead of a pointer. For an `Ordered`
  /// within groups, the walk stays in the group of the row that the key identifies. If the row that the key
  /// identifies may have no row at that position, use `next`, which returns a null pointer in that case.
  ///
  /// Takes: `element_and_steps` must be a key of the set, as the Keys section describes, and may be followed by a
  /// step count. A step count must be an integer other than `bool`, or a column of integers. If the step count is
  /// left out, it is 1. A negative step count walks the other way. The `Ordered` must be an lvalue.
  /// Contract: ordered_neighbour_is_there
  template <typename... Args>
  [[nodiscard]] const Tup& nextOf(const Args&... element_and_steps) const& {
    return neighbourOf(walk<false, false>(element_and_steps...), "nextOf");
  }

  /// Returns the row a given number of steps before the row that the key identifies.
  ///
  /// `prevOf` walks in the same way as `prev`, but returns the row itself instead of a pointer. For an `Ordered`
  /// within groups, the walk stays in the group of the row that the key identifies. If the row that the key
  /// identifies may have no row at that position, use `prev`, which returns a null pointer in that case.
  ///
  /// Takes: `element_and_steps` must be a key of the set, as the Keys section describes, and may be followed by a
  /// step count. A step count must be an integer other than `bool`, or a column of integers. If the step count is
  /// left out, it is 1. A negative step count walks the other way. The `Ordered` must be an lvalue.
  /// Contract: ordered_neighbour_is_there
  template <typename... Args>
  [[nodiscard]] const Tup& prevOf(const Args&... element_and_steps) const& {
    return neighbourOf(walk<false, true>(element_and_steps...), "prevOf");
  }

  /// Returns a pointer to the row a given number of steps after the row that the key identifies, wrapping around the
  /// ends.
  ///
  /// A walk past the last row continues from the first row, and a walk past the first row continues from the last
  /// row. For an `Ordered` within groups, the walk wraps around the ends of the group of the row that the key
  /// identifies.
  ///
  /// Takes: `element_and_steps` must be a key of the set, as the Keys section describes, and may be followed by a
  /// step count. A step count must be an integer other than `bool`, or a column of integers. If the step count is
  /// left out, it is 1. A negative step count walks toward the first row. The step count may be greater than the
  /// number of rows, and the walk then wraps around as many times as needed. The `Ordered` must be an lvalue.
  /// Returns: A pointer to a row that the `Ordered` holds, valid while the `Ordered` exists. A null pointer only if no
  /// row of the set has the key.
  template <typename... Args>
  [[nodiscard]] const Tup* nextCyclic(const Args&... element_and_steps) const& {
    return walk<true, false>(element_and_steps...);
  }

  /// Returns a pointer to the row a given number of steps before the row that the key identifies, wrapping around the
  /// ends.
  ///
  /// `prevCyclic` walks in the direction opposite to `nextCyclic`, and otherwise behaves as `nextCyclic` does. A walk
  /// past the first row continues from the last row, and a walk past the last row continues from the first row. For
  /// an `Ordered` within groups, the walk wraps around the ends of the group of the row that the key identifies.
  ///
  /// Takes: `element_and_steps` must be a key of the set, as the Keys section describes, and may be followed by a
  /// step count. A step count must be an integer other than `bool`, or a column of integers. If the step count is
  /// left out, it is 1. A negative step count walks toward the last row. The step count may be greater than the
  /// number of rows, and the walk then wraps around as many times as needed. The `Ordered` must be an lvalue.
  /// Returns: A pointer to a row that the `Ordered` holds, valid while the `Ordered` exists. A null pointer only if no
  /// row of the set has the key.
  template <typename... Args>
  [[nodiscard]] const Tup* prevCyclic(const Args&... element_and_steps) const& {
    return walk<true, true>(element_and_steps...);
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename... Args>
  const Tup* next(const Args&... /*element_and_steps*/) const&& {
    static_assert(internal::always_false<Args...>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename... Args>
  const Tup* prev(const Args&... /*element_and_steps*/) const&& {
    static_assert(internal::always_false<Args...>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename... Args>
  const Tup& nextOf(const Args&... /*element_and_steps*/) const&& {
    static_assert(internal::always_false<Args...>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename... Args>
  const Tup& prevOf(const Args&... /*element_and_steps*/) const&& {
    static_assert(internal::always_false<Args...>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename... Args>
  const Tup* nextCyclic(const Args&... /*element_and_steps*/) const&& {
    static_assert(internal::always_false<Args...>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a call on an rvalue Ordered with a message
  template <typename... Args>
  const Tup* prevCyclic(const Args&... /*element_and_steps*/) const&& {
    static_assert(internal::always_false<Args...>, DREAMOR_INTERNAL_NAME_THE_ORDERED_MESSAGE);
    std::terminate();
  }

  /// Checks whether the row that the key identifies is the first row of the set.
  ///
  /// For an `Ordered` within groups, `isFirst` checks whether the row is the first row of its group.
  ///
  /// Takes: `element` must be a key of the set, as the Keys section describes.
  /// Returns: True if a row of the set has the key and that row is the first row of the set or of its group; false
  /// otherwise.
  template <typename... Args>
  [[nodiscard]] bool isFirst(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      return positionOf(element...) == std::optional<std::size_t>{0};
    } else {
      return false;
    }
  }

  /// Checks whether the row that the key identifies is the last row of the set.
  ///
  /// For an `Ordered` within groups, `isLast` checks whether the row is the last row of its group.
  ///
  /// Takes: `element` must be a key of the set, as the Keys section describes.
  /// Returns: True if a row of the set has the key and that row is the last row of the set or of its group; false
  /// otherwise.
  template <typename... Args>
  [[nodiscard]] bool isLast(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      const auto position = positionInMembers(internal::keyOf<Tup>(element...));
      return position.has_value() && *position + 1 == spanOf(*position).second;
    } else {
      return false;
    }
  }

  /// Checks whether a row of the set has the key.
  ///
  /// Takes: `element` must be a key of the set, as the Keys section describes.
  /// Returns: True if a row of the set has the key; false otherwise.
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      return positionInMembers(internal::keyOf<Tup>(element...)).has_value();
    } else {
      return false;
    }
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] std::optional<std::size_t> positionOf(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup* next(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup* prev(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup& nextOf(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup& prevOf(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup* nextCyclic(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] const Tup* prevCyclic(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] bool isFirst(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] bool isLast(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

  /// @hidden - exists only to reject a key written in braces with a message
  template <typename Unused = void>
  [[nodiscard]] bool contains(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

 private:
  static constexpr std::size_t kNotMember = ::dreamor::internal::FlatIndex<Tup>::kNotFound;

  [[nodiscard]] auto rowsAt() const {
    return [this](std::size_t position) -> const Tup& { return rows()[position]; };
  }

  [[nodiscard]] auto groupKeysAt() const {
    return [this](std::size_t group) -> const GroupKey& { return group_keys_[group]; };
  }

  /// @contract ordered_set_has_row: An `Ordered` holds at least one row when its `first()` or `last()` is called.

  template <bool First>
  [[nodiscard]] const Tup& setEnd([[maybe_unused]] const char* what) const {
    if (rows().empty()) {
      DREAMOR_DEBUG_FATAL(
          "Ordered::{} has no row to return - a broken contract: the set is empty. Ask it of a set that has rows - "
          "check with empty() first.",
          what);
      internal::unreachable();
    }
    return First ? rows().front() : rows().back();
  }

  /// @contract ordered_group_has_row: When `first` or `last` of an `Ordered` is called with a key, at least one row of
  /// the set has that key in the columns `Within...`.

  template <bool First, typename... Args>
  [[nodiscard]] const Tup& groupEnd([[maybe_unused]] const char* what, const Args&... group) const {
    static_assert(
        kWithinGroups,
        "first(m) and last(m) name a group's ends under orderedWithin<Columns...>(). Over the whole set, first() and "
        "last() take no argument.");
    if constexpr (kWithinGroups && internal::checkOrderedGroup<GroupKey, Args...>()) {
      const auto key          = internal::keyOf<GroupKey>(group...);
      const std::size_t index = key ? group_index_.find(*key, groupKeysAt()) : GroupIndex::kNotFound;
      if (index == GroupIndex::kNotFound) {
        DREAMOR_DEBUG_FATAL(
            "Ordered::{} has no row to return - a broken contract: the group has no row in the set. Run it over the "
            "groups that have rows - for orderedWithin<cMachine>(), the machines of sRUN.select<cMachine>().",
            what);
        internal::unreachable();
      }
      return rows()[First ? group_starts_[index] : group_starts_[index + 1] - 1];
    } else {
      std::terminate();
    }
  }

  // Returns the position of the row in members_, or nullopt if the set does not hold the row. An empty element stands
  // for a key that no set can hold, such as an integer outside its column's range.
  [[nodiscard]] std::optional<std::size_t> positionInMembers(const std::optional<Tup>& element) const {
    if (!element) { return std::nullopt; }
    const std::size_t position = positions_.find(*element, rowsAt());
    return (position == kNotMember) ? std::nullopt : std::optional{position};
  }

  // Returns the positions [first, last) that a walk from `position` stays within: those of its group, or of the whole
  // set.
  [[nodiscard]] std::pair<std::size_t, std::size_t> spanOf(std::size_t position) const {
    if constexpr (kWithinGroups) {
      const std::size_t group = group_of_[position];
      return {group_starts_[group], group_starts_[group + 1]};
    } else {
      return {0, members_.size()};
    }
  }

  // Returns the row `steps.count` positions after `element`, or before it if `steps.backward`, within the span of
  // `element`. If `cyclic` is true, the walk wraps around the ends of the span. Returns nullptr if the set does not
  // hold `element`, or if the walk is not cyclic and ends outside the span. The count may exceed the span by any
  // amount, so it is compared with the room on each side, or taken modulo the span, before it is added: no sum can
  // overflow.
  [[nodiscard]] const Tup* stepFrom(const std::optional<Tup>& element, internal::Steps steps, bool cyclic) const {
    const auto position = positionInMembers(element);
    if (!position.has_value()) { return nullptr; }
    const auto [first, last]    = spanOf(*position);
    const std::uintmax_t count  = last - first;  // at least 1: the span holds element
    const std::uintmax_t offset = *position - first;
    std::uintmax_t target       = 0;
    if (cyclic) {
      const std::uintmax_t wrapped = steps.count % count;
      target                       = (steps.backward ? offset + count - wrapped : offset + wrapped) % count;
    } else if (steps.backward) {
      if (steps.count > offset) { return nullptr; }
      target = offset - steps.count;
    } else {
      if (steps.count >= count - offset) { return nullptr; }
      target = offset + steps.count;
    }
    // target is less than count, so it fits in std::size_t, which std::uintmax_t is on a 64-bit platform.
    if constexpr (std::is_same_v<std::uintmax_t, std::size_t>) {
      return &rows()[first + target];
    } else {
      return &rows()[first + static_cast<std::size_t>(target)];
    }
  }

  /// @contract ordered_neighbour_is_there: When `nextOf` or `prevOf` of an `Ordered` is called, the key identifies a
  /// row of the set, and the walk from that row ends on a row of the same set or, within groups, of the same group.

  [[nodiscard]] static const Tup& neighbourOf(const Tup* row, [[maybe_unused]] const char* what) {
    if (row == nullptr) {
      DREAMOR_DEBUG_FATAL(
          "Ordered::{} has no row to return - a broken contract: the element is past the end of its set or group, "
          "or not in it. Run it over a domain that has the neighbour (isFirst, isLast), or use the pointer form, "
          "next / prev.",
          what);
      internal::unreachable();
    }
    return *row;
  }

  template <bool Cyclic, bool Backward, typename... Args>
  [[nodiscard]] const Tup* walk(const Args&... element_and_steps) const {
    if constexpr (internal::checkOrderedStep<Tup, Args...>()) {
      if constexpr (internal::isKey<Tup, Args...>()) {
        return stepFrom(internal::keyOf<Tup>(element_and_steps...), internal::Steps{.backward = Backward}, Cyclic);
      } else {
        auto [element, steps] = internal::keyAndSteps<Tup>(element_and_steps...);
        steps.backward        = steps.backward != Backward;  // prev walks the other way
        return stepFrom(element, steps, Cyclic);
      }
    } else {
      return nullptr;
    }
  }

  [[nodiscard]] const internal::Rows<Tup>& rows() const noexcept { return internal::SetAccess::rows(members_); }

  // The set itself, or within groups, its rows laid out group by group.
  Set members_ = internal::SetAccess::empty<Tup>();
  // The index from a row to its position in members_.
  ::dreamor::internal::FlatIndex<Tup> positions_;
  // The members below are filled only within groups.
  // The group of each position. std::uint32_t is enough, because Grouped accepts at most 2^32 - 2 rows.
  std::vector<std::uint32_t> group_of_;
  // The first position of each group, then the number of rows: K + 1 entries for K groups.
  std::vector<std::size_t> group_starts_;
  // The key of each group, in the order of the groups.
  std::vector<GroupKey> group_keys_;
  // The index from a group's key to the group's number in group_keys_.
  GroupIndex group_index_;
};

template <typename... Columns>
Ordered<IndexSet<Columns...>> IndexSet<Columns...>::ordered() const {
  return Ordered<IndexSet>{*this};
}

template <typename... Columns>
template <typename... Within>
Ordered<IndexSet<Columns...>, Within...> IndexSet<Columns...>::orderedWithin() const {
  static_assert(sizeof...(Within) > 0,
                "orderedWithin<Columns...>() names the columns whose groups it orders within; S.ordered() orders the "
                "whole set.");
  internal::checkColumns<Tup, Within...>();
  return Ordered<IndexSet, Within...>{*this};
}

template <typename... Columns>
template <typename First, typename... Rest>
Ordered<IndexSet<Columns...>> IndexSet<Columns...>::ordered() const {
  static_assert(internal::always_false<First>,
                "ordered() takes no columns: positions within each group of rows that agree on some columns are "
                "orderedWithin<Columns...>() - S.orderedWithin<cMachine>().");
  return Ordered<IndexSet>{*this};
}

}  // namespace dreamor
