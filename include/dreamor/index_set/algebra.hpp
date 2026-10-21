// SPDX-License-Identifier: Apache-2.0
// File: algebra.hpp
// Set algebra over materialized index sets.
//
// Contract: every operation preserves first-appearance order of the left operand, then of
// the right. Hashing decides membership only, never output order, so results are
// reproducible even when tuples hold pointers.
//
// Every operation returns a new IndexSet and leaves its operands as they were. Binary
// operations are free functions (their operands are symmetric); unary ones are members of
// IndexSet, defined here: orderBy, distinct, select, membership, assertUnique.
#pragma once

#include <algorithm>  // IWYU pragma: keep // For std::ranges::stable_sort
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dreamor/general/tuple_hash.hpp"
#include "dreamor/index_set/column.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"

namespace dreamor {

// Appends without deduplicating; see unite() for A ∪ B.
template <typename Tup>
[[nodiscard]] IndexSet<Tup> concat(const IndexSet<Tup>& first, const IndexSet<Tup>& second) {
  std::vector<Tup> out;
  out.reserve(first.size() + second.size());
  out.insert(out.end(), first.begin(), first.end());
  out.insert(out.end(), second.begin(), second.end());
  return internal::SetAccess::make(std::move(out));
}

namespace internal {

template <typename... Columns, typename Tup>
[[nodiscard]] auto pickColumns(const IndexSet<Tup>& set) {
  std::vector<std::tuple<Columns...>> out;
  out.reserve(set.size());
  for (const auto& tuple : set) {
    out.emplace_back(std::get<Columns>(tuple)...);
  }
  return out;
}

}  // namespace internal

namespace internal {
// First occurrence of each tuple, front to back.
template <typename Tup>
[[nodiscard]] std::vector<Tup> firstOccurrences(const std::vector<Tup>& rows) {
  std::unordered_set<Tup, ::dreamor::general::TupleHash> seen;
  seen.reserve(rows.size());
  std::vector<Tup> out;
  out.reserve(rows.size());
  for (const auto& tuple : rows) {
    if (seen.insert(tuple).second) {
      out.push_back(tuple);
    }
  }
  return out;
}
}  // namespace internal

// Keeps the first occurrence of each row. A set with no repeats comes back as itself.
template <typename Tup>
IndexSet<Tup> IndexSet<Tup>::distinct() const {
  auto rows = internal::firstOccurrences(internal::SetAccess::rows(*this));
  if (rows.size() == size()) {
    return *this;
  }
  return internal::SetAccess::make(std::move(rows));
}

// Projection onto some columns, as a true set: S.select<MachineCol, ItemCol>(). Naming every
// column in another order reorders the columns: S.select<OrderCol, MachineCol, ItemCol>().
template <typename Tup>
template <typename... Columns>
IndexSet<std::tuple<Columns...>> IndexSet<Tup>::select() const {
  static_assert(sizeof...(Columns) > 0, "select() needs at least one column.");
  internal::checkColumns<Tup, Columns...>();
  return internal::SetAccess::make(internal::firstOccurrences(internal::pickColumns<Columns...>(*this)));
}

// A ∪ B
template <typename Tup>
[[nodiscard]] IndexSet<Tup> unite(const IndexSet<Tup>& left, const IndexSet<Tup>& right) {
  std::unordered_set<Tup, ::dreamor::general::TupleHash> seen;
  seen.reserve(left.size() + right.size());
  std::vector<Tup> out;
  out.reserve(left.size() + right.size());
  for (const auto* side : {&left, &right}) {
    for (const auto& tuple : *side) {
      if (seen.insert(tuple).second) {
        out.push_back(tuple);
      }
    }
  }
  return internal::SetAccess::make(std::move(out));
}

// A ∩ B
template <typename Tup>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what intersecting means
[[nodiscard]] IndexSet<Tup> intersect(const IndexSet<Tup>& left, const IndexSet<Tup>& right) {
  const std::unordered_set<Tup, ::dreamor::general::TupleHash> rhs{right.begin(), right.end()};
  std::unordered_set<Tup, ::dreamor::general::TupleHash> emitted;
  std::vector<Tup> out;
  for (const auto& tuple : left) {
    if (rhs.contains(tuple) && emitted.insert(tuple).second) {
      out.push_back(tuple);
    }
  }
  return internal::SetAccess::make(std::move(out));
}

// A \ B
template <typename Tup>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what A \ B means
[[nodiscard]] IndexSet<Tup> difference(const IndexSet<Tup>& left, const IndexSet<Tup>& right) {
  const std::unordered_set<Tup, ::dreamor::general::TupleHash> rhs{right.begin(), right.end()};
  std::unordered_set<Tup, ::dreamor::general::TupleHash> emitted;
  std::vector<Tup> out;
  for (const auto& tuple : left) {
    if (!rhs.contains(tuple) && emitted.insert(tuple).second) {
      out.push_back(tuple);
    }
  }
  return internal::SetAccess::make(std::move(out));
}

// O(1) membership test, so one set can filter another during generation instead of
// intersecting afterwards:
//
//     const auto in_supply = sSUPPLY.membership();       // over (NodeCol, PeriodCol)
//     makeSet<NodeCol, PeriodCol>(nodes, periods).where(in_supply)
//
// A membership over fewer columns prunes earlier: one over (NodeCol) alone runs as soon as
// the node is bound.
template <typename Tup>
class Membership {
 public:
  explicit Membership(const IndexSet<Tup>& set) : members_{set.begin(), set.end()} {}

  // The set's columns, in order - where() calls pred(a, b), and so can you: in_routes(m, i) -
  // or a whole row (see key_args.hpp). The parameter list cannot say which columns these are
  // to where(); PredicateColumns below does that.
  template <typename... Args>
  [[nodiscard]] bool operator()(const Args&... key) const {
    if constexpr (internal::checkMembershipKey<Tup, Args...>()) {
      return members_.contains(internal::keyOf<Tup>(key...));
    } else {
      return false;
    }
  }

 private:
  std::unordered_set<Tup, ::dreamor::general::TupleHash> members_;
};

// Declares this membership's columns, so where() places it by them rather than by probing
// its signature - which would match any call with the right number of arguments.
template <typename Tup>
struct PredicateColumns<Membership<Tup>> {
  using type = Tup;
};

template <typename Tup>
Membership<Tup> IndexSet<Tup>::membership() const {
  return Membership<Tup>{*this};
}

// Debug-only key check, compiled out under NDEBUG. This library keeps duplicates unless
// asked, so nothing stops a set from holding the same tuple twice - and a duplicate is
// silent: it just counts twice in every sum and shows up twice in every group.
//
//     const auto sASSIGN = makeSet<MachineCol, ItemCol>(machines, items_of).collect().assertUnique();
//     sASSIGN.assertUnique<MachineCol, ItemCol>();   // these columns are a key
//
// With no column types the whole tuple must be unique; with some, that projection must be.
// Returns the same set, so it drops into a declaration.
template <typename Tup>
template <typename... Columns>
IndexSet<Tup> IndexSet<Tup>::assertUnique() const {  // NOLINT(modernize-use-nodiscard): see the declaration
#ifndef NDEBUG
  const auto& rows = internal::SetAccess::rows(*this);
  if constexpr (sizeof...(Columns) > 0) {
    internal::checkColumns<Tup, Columns...>();
  }
  using Key = std::conditional_t<sizeof...(Columns) == 0, Tup, std::tuple<Columns...>>;
  std::unordered_map<Key, std::size_t, ::dreamor::general::TupleHash> first;
  first.reserve(rows.size());
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Key key = [&] {
      if constexpr (sizeof...(Columns) == 0) {
        return rows[i];
      } else {
        return Key{std::get<Columns>(rows[i])...};
      }
    }();
    const auto [it, inserted] = first.try_emplace(std::move(key), i);
    if (!inserted) {
      throw std::logic_error("assertUnique: row " + std::to_string(i) + " repeats row " + std::to_string(it->second) +
                             (sizeof...(Columns) == 0 ? "." : " on the named columns, so they are not a key."));
    }
  }
#endif
  return *this;
}

// Stable reordering into "<key>-major" traversal order. Pass an explicit key (an id, a
// name): a key that falls back on pointer comparison orders by address and varies between
// runs.
//
//     const auto sBY_NAME = sITEMS.orderBy([](const auto& row) { return std::get<ItemCol>(row)->name; });
//
// The key receives the whole row, so it is the one callable here that may be generic.
template <typename Tup>
template <typename Key>
IndexSet<Tup> IndexSet<Tup>::orderBy(Key key) const {
  static_assert(std::invocable<Key&, const Tup&>, "orderBy(key): key(row) must be callable with a row of this set.");
  std::vector<Tup> rows{begin(), end()};
  std::ranges::stable_sort(rows, {}, key);
  return internal::SetAccess::make(std::move(rows));
}

}  // namespace dreamor
