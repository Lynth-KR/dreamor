// SPDX-License-Identifier: Apache-2.0
// File: ordered.hpp
// Positional view over an index set whose row order IS its ordering - the ord / next /
// prev of an algebraic modeling language.
#pragma once

#include <cstddef>
#include <exception>
#include <optional>  // IWYU pragma: keep // For std::optional in ord()
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"
#include "dreamor/index_set/algebra.hpp"
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
template <typename Tup>
class Ordered {
 public:
  // One position per element: a set holds no row twice (index_set.hpp), so insertAll goes
  // through.
  explicit Ordered(const IndexSet<Tup>& set) : members_{set}, positions_{members_.size()} {
    static_cast<void>(positions_.insertAll(0, internal::SetAccess::rows(members_).size(), rowsAt()));
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

  [[nodiscard]] const Tup& at(std::size_t position) const& { return rows().at(position); }
  template <typename Unused = void>
  [[nodiscard]] const Tup& at(std::size_t /*position*/) const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }

  [[nodiscard]] const Tup& first() const& { return rows().front(); }
  template <typename Unused = void>
  [[nodiscard]] const Tup& first() const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }

  [[nodiscard]] const Tup& last() const& { return rows().back(); }
  template <typename Unused = void>
  [[nodiscard]] const Tup& last() const&& {
    static_assert(
        internal::always_false<Unused>,
        "Name the Ordered first - const auto orderedPeriods = sPERIODS.ordered(); - its lookups hand out pointers into it, which would dangle once a temporary is gone.");
    std::terminate();
  }

  // An element is named by the set's columns themselves, in order - orderedPeriods.ord(t) -
  // or by a row of the set (see key_args.hpp).

  // 0-based position, or nullopt for a non-member.
  template <typename... Args>
  [[nodiscard]] std::optional<std::size_t> ord(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      return ordOf(internal::keyOf<Tup>(element...));
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
      return ordOf(internal::keyOf<Tup>(element...)) == std::optional<std::size_t>{0};
    } else {
      return false;
    }
  }
  template <typename... Args>
  [[nodiscard]] bool isLast(const Args&... element) const {
    if constexpr (internal::checkOrderedKey<Tup, Args...>()) {
      return members_.size() != 0 && ordOf(internal::keyOf<Tup>(element...)) == std::optional{members_.size() - 1};
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

  [[nodiscard]] std::optional<std::size_t> ordOf(const Tup& element) const {
    const std::size_t position = positions_.find(element, rowsAt());
    return (position == kNotMember) ? std::nullopt : std::optional{position};
  }

  // The element `steps` away from `element`, wrapping when Cyclic; nullptr for a non-member,
  // past either end (not Cyclic) or in an empty set.
  [[nodiscard]] const Tup* stepFrom(const Tup& element, std::ptrdiff_t steps, bool cyclic) const {
    const auto position = ordOf(element);
    if (!position.has_value() || members_.size() == 0) { return nullptr; }
    const auto count = static_cast<std::ptrdiff_t>(members_.size());
    auto target      = static_cast<std::ptrdiff_t>(*position) + steps;
    if (cyclic) {
      target %= count;
      if (target < 0) { target += count; }
    } else if (target < 0 || target >= count) {
      return nullptr;
    }
    return &rows()[static_cast<std::size_t>(target)];
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

  IndexSet<Tup> members_;
  ::dreamor::general::FlatIndex<Tup> positions_;  // Element -> position, over members_' rows.
};

template <typename Tup>
Ordered<Tup> IndexSet<Tup>::ordered() const {
  return Ordered<Tup>{*this};
}

}  // namespace dreamor
