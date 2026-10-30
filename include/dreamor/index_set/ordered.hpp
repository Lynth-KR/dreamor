// SPDX-License-Identifier: Apache-2.0
// File: ordered.hpp
// Positional view over an index set whose row order IS its ordering - the ord / next /
// prev of an algebraic modeling language - over the whole set, or within each group of rows
// that agree on some columns.
#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>  // IWYU pragma: keep // For std::optional in ord()
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"
#include "dreamor/index_set/algebra.hpp"
#include "dreamor/index_set/grouped.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"

namespace dreamor {

// For when an element arrives from somewhere else and you have to reach its neighbour:
//
//     const auto orderedPeriods = sPERIODS.ordered();
//     for (const auto& [item, period] : sSTOCK) {
//       if (const Tup* prevPeriod = orderedPeriods.prev(period)) {
//         model.add(vStock(item, period) == vStock(item, std::get<0>(*prevPeriod)) + ...);
//       }
//     }
//
// Positions are 0-based. Holds the set itself, sharing its rows.
//
// S.ordered<MachineCol>() orders within each machine's rows instead - AMPL's next(t, S[m]) over an
// indexed collection of ordered sets: a row's neighbours are the rows of its own group, in the
// set's order, and the walk stops (or, cyclic, wraps) at the group's ends. ord() is then the
// position within the group, isFirst / isLast the group's ends; at(), first() and last(), which
// name positions of the whole set, are not there.
//
//     const auto periodsOfMachine = sRUN.ordered<MachineCol>();
//     if (const Tup* next = periodsOfMachine.next(m, t)) { ... }   // the period after t among m's
template <typename Tup, typename... Within>
class Ordered {
  static constexpr bool kWithinGroups = sizeof...(Within) > 0;

 public:
  // One position per element: a set holds no row twice (index_set.hpp), so insertAll goes
  // through. Within groups, the rows are laid out group by group - the groups in order of first
  // appearance, each in the set's order - and every position knows its group's span.
  explicit Ordered(const IndexSet<Tup>& set) {
    if constexpr (kWithinGroups) {
      const Grouped<Tup, Within...> groups{set};
      members_ = groups.regrouped();
      group_starts_.reserve(groups.size() + 1);
      group_of_.reserve(members_.size());
      group_starts_.push_back(0);
      for (const auto& group : groups) {
        group_of_.insert(group_of_.end(), group.tuples.size(), static_cast<std::uint32_t>(group_starts_.size() - 1));
        group_starts_.push_back(group_starts_.back() + group.tuples.size());
      }
    } else {
      members_ = set;
    }
    positions_ = ::dreamor::general::FlatIndex<Tup>{members_.size()};
    static_cast<void>(positions_.insertAll(0, rows().size(), rowsAt()));
  }

  [[nodiscard]] std::size_t size() const noexcept { return members_.size(); }
  [[nodiscard]] bool empty() const noexcept { return members_.size() == 0; }

  // On rvalues these stop with a sentence, for the same reason as in Grouped: they borrow
  // from members_, so `S.ordered().prev(t)` would point into a vector that is already gone.
  [[nodiscard]] const IndexSet<Tup>& members() const& noexcept { return members_; }
  template <typename Unused = void>
  [[nodiscard]] const IndexSet<Tup>& members() const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }

  [[nodiscard]] const Tup& at(std::size_t position) const& {
    static_assert(
        !kWithinGroups,
        "ordered<Columns...>() orders within each group: at(), first() and last() name positions of the whole set. Use ord(), isFirst() and isLast() within a group, or S.ordered() for the whole set.");
    return rows().at(position);
  }
  template <typename Unused = void>
  [[nodiscard]] const Tup& at(std::size_t /*position*/) const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }

  [[nodiscard]] const Tup& first() const& {
    static_assert(
        !kWithinGroups,
        "ordered<Columns...>() orders within each group: at(), first() and last() name positions of the whole set. Use ord(), isFirst() and isLast() within a group, or S.ordered() for the whole set.");
    return rows().front();
  }
  template <typename Unused = void>
  [[nodiscard]] const Tup& first() const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }

  [[nodiscard]] const Tup& last() const& {
    static_assert(
        !kWithinGroups,
        "ordered<Columns...>() orders within each group: at(), first() and last() name positions of the whole set. Use ord(), isFirst() and isLast() within a group, or S.ordered() for the whole set.");
    return rows().back();
  }
  template <typename Unused = void>
  [[nodiscard]] const Tup& last() const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }

  // An element is named by the set's columns themselves, in order - orderedPeriods.ord(t) -
  // or by a row of the set (see key_args.hpp).

  // 0-based position - within its group, under ordered<Within...>() - or nullopt for a non-member.
  template <typename... Args>
  [[nodiscard]] std::optional<std::size_t> ord(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      const auto position = positionOf(internal::keyOf<Tup>(element...));
      if (!position.has_value()) { return std::nullopt; }
      return *position - spanOf(*position).first;
    } else {
      return std::nullopt;
    }
  }

  // element + steps, or nullptr at the boundary or for a non-member: next(t), next(t, 2).
  // Negative steps walk backwards. The pointer is into this object's storage, so it is valid
  // while this lives.
  template <typename... Args>
  [[nodiscard]] const Tup* next(const Args&... element_and_steps) const& {
    return walk<false, 1>(element_and_steps...);
  }
  template <typename... Args>
  [[nodiscard]] const Tup* prev(const Args&... element_and_steps) const& {
    return walk<false, -1>(element_and_steps...);
  }
  // Wrapping variants, for cyclic horizons. Only null for a non-member or empty set.
  template <typename... Args>
  [[nodiscard]] const Tup* nextCyclic(const Args&... element_and_steps) const& {
    return walk<true, 1>(element_and_steps...);
  }
  template <typename... Args>
  [[nodiscard]] const Tup* prevCyclic(const Args&... element_and_steps) const& {
    return walk<true, -1>(element_and_steps...);
  }
  template <typename... Args>
  const Tup* next(const Args&... /*element_and_steps*/) const&& {
    static_assert(
        internal::always_false<Args...>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }
  template <typename... Args>
  const Tup* prev(const Args&... /*element_and_steps*/) const&& {
    static_assert(
        internal::always_false<Args...>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }
  template <typename... Args>
  const Tup* nextCyclic(const Args&... /*element_and_steps*/) const&& {
    static_assert(
        internal::always_false<Args...>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }
  template <typename... Args>
  const Tup* prevCyclic(const Args&... /*element_and_steps*/) const&& {
    static_assert(
        internal::always_false<Args...>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }

  template <typename... Args>
  [[nodiscard]] bool isFirst(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      return ord(element...) == std::optional<std::size_t>{0};
    } else {
      return false;
    }
  }
  template <typename... Args>
  [[nodiscard]] bool isLast(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      const auto position = positionOf(internal::keyOf<Tup>(element...));
      return position.has_value() && *position + 1 == spanOf(*position).second;
    } else {
      return false;
    }
  }
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      return positions_.find(internal::keyOf<Tup>(element...), rowsAt()) != kNotMember;
    } else {
      return false;
    }
  }
  // prev({t}) and the like: braces are not an element.
  template <typename Unused = void>
  [[nodiscard]] std::optional<std::size_t> ord(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] const Tup* next(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] const Tup* prev(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] const Tup* nextCyclic(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] const Tup* prevCyclic(internal::BracedKey /*key*/, std::ptrdiff_t /*steps*/ = 1) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] bool isFirst(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] bool isLast(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }
  template <typename Unused = void>
  [[nodiscard]] bool contains(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    std::terminate();
  }

 private:
  static constexpr std::size_t kNotMember = ::dreamor::general::FlatIndex<Tup>::kNotFound;

  [[nodiscard]] auto rowsAt() const {
    return [this](std::size_t position) -> const Tup& { return rows()[position]; };
  }

  // The element's position among the laid-out rows, or nullopt for a non-member.
  [[nodiscard]] std::optional<std::size_t> positionOf(const Tup& element) const {
    const std::size_t position = positions_.find(element, rowsAt());
    return (position == kNotMember) ? std::nullopt : std::optional{position};
  }

  // [first, last) of the positions a walk from `position` stays within: its group's, or the set's.
  [[nodiscard]] std::pair<std::size_t, std::size_t> spanOf(std::size_t position) const {
    if constexpr (kWithinGroups) {
      const std::size_t group = group_of_[position];
      return {group_starts_[group], group_starts_[group + 1]};
    } else {
      return {0, members_.size()};
    }
  }

  // The element `steps` away from `element` within its span, wrapping when Cyclic; nullptr for a
  // non-member or past either end (not Cyclic).
  [[nodiscard]] const Tup* stepFrom(const Tup& element, std::ptrdiff_t steps, bool cyclic) const {
    const auto position = positionOf(element);
    if (!position.has_value()) { return nullptr; }
    const auto [first, last] = spanOf(*position);
    const auto count         = static_cast<std::ptrdiff_t>(last - first);
    auto target              = static_cast<std::ptrdiff_t>(*position - first) + steps;
    if (cyclic) {
      target %= count;
      if (target < 0) { target += count; }
    } else if (target < 0 || target >= count) {
      return nullptr;
    }
    return &rows()[first + static_cast<std::size_t>(target)];
  }

  template <bool Cyclic, std::ptrdiff_t Direction, typename... Args>
  [[nodiscard]] const Tup* walk(const Args&... element_and_steps) const {
    if constexpr (internal::checkOrderedStep<Tup, Args...>()) {
      if constexpr (internal::isKey<Tup, Args...>()) {
        return stepFrom(internal::keyOf<Tup>(element_and_steps...), Direction, Cyclic);
      } else {
        const auto [element, steps] = internal::keyAndSteps<Tup>(element_and_steps...);
        return stepFrom(element, Direction * steps, Cyclic);
      }
    } else {
      return nullptr;
    }
  }

  [[nodiscard]] const std::vector<Tup>& rows() const noexcept { return internal::SetAccess::rows(members_); }

  IndexSet<Tup> members_;                         // Within groups, the set laid out group by group.
  ::dreamor::general::FlatIndex<Tup> positions_;  // Element -> position, over members_' rows.
  std::vector<std::uint32_t> group_of_;           // Within groups: each position's group (groupBy's limit).
  std::vector<std::size_t> group_starts_;         // Within groups: size K + 1, each group's first position.
};

// S.ordered() over the whole set; S.ordered<MachineCol>() within each machine's rows.
template <typename Tup>
template <typename... Within>
Ordered<Tup, Within...> IndexSet<Tup>::ordered() const {
  if constexpr (sizeof...(Within) > 0) { internal::checkColumns<Tup, Within...>(); }
  return Ordered<Tup, Within...>{*this};
}

}  // namespace dreamor
