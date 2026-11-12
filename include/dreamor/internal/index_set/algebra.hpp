// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The set algebra and Membership, and the members of IndexSet that this file defines: select, membership, contains
// and orderBy.
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/base/flat_index.hpp"
#include "dreamor/internal/index_set/binding.hpp"
#include "dreamor/internal/index_set/call.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_SAME_COLUMNS_MESSAGE                                                                  \
  "unite, intersect, difference and within take two sets of the same columns, in any order. To drop the rows " \
  "a set of some of the columns holds, filter: S.where(!sOFF.membership()); to keep them, "                    \
  "S.where(sON.membership())."

namespace dreamor::internal::inline index_set {

template <typename... Columns, typename Set>
[[nodiscard]] auto pickColumns(const Set& set) {
  Rows<std::tuple<Columns...>> out;
  out.reserve(set.size());
  for (const auto& tuple : set) {
    out.emplace_back(std::get<Columns>(tuple)...);
  }
  return out;
}

template <typename Tup>
using TupleIndex = ::dreamor::internal::FlatIndex<Tup>;

// Returns the key_at function of a FlatIndex whose entries are positions in rows.
template <typename Tup>
[[nodiscard]] auto rowsAt(const Rows<Tup>& rows) {
  return [&rows](std::size_t row) -> const Tup& { return rows[row]; };
}

// Returns an index of the rows by position. If a row repeats, the index keeps its first position. The rows are hashed
// a block ahead with hashedBlocks, so that the cache misses on a large index overlap.
template <typename Tup>
[[nodiscard]] TupleIndex<Tup> indexRows(const Rows<Tup>& rows) {
  TupleIndex<Tup> index{rows.size()};
  const auto at = rowsAt(rows);
  ::dreamor::internal::hashedBlocks(
      0,
      rows.size(),
      at,
      [&](std::size_t row, std::size_t hash) {
        static_cast<void>(index.insert(rows[row], hash, row, at));
        return true;
      },
      index);
  return index;
}

// Returns the index of a set's rows by value, which every copy of the set shares. contains() and Membership look up
// rows in it. It is built on its first use and is kept with the rows.
template <typename... Columns>
[[nodiscard]] const TupleIndex<std::tuple<Columns...>>& sharedIndex(const IndexSet<Columns...>& set) {
  const auto make_index = [](const Rows<std::tuple<Columns...>>& rows) {
    return std::make_unique<const TupleIndex<std::tuple<Columns...>>>(indexRows(rows));
  };
  return SetAccess::lookup(set, make_index);
}

// Appends tuple to out unless out already holds it. hash is the hash of tuple, and seen is the index of out.
template <typename Tup>
void appendNew(Rows<Tup>& out, TupleIndex<Tup>& seen, const Tup& tuple, std::size_t hash) {
  if (seen.insert(tuple, hash, out.size(), rowsAt(out)) == TupleIndex<Tup>::kNotFound) { out.push_back(tuple); }
}

// Appends to out each row of rows that out does not hold yet, in the order of rows. If a row repeats, only its first
// occurrence is appended.
template <typename Tup>
void appendFirstOccurrences(Rows<Tup>& out, TupleIndex<Tup>& seen, const Rows<Tup>& rows) {
  ::dreamor::internal::hashedBlocks(
      0,
      rows.size(),
      rowsAt(rows),
      [&](std::size_t row, std::size_t hash) {
        appendNew(out, seen, rows[row], hash);
        return true;
      },
      seen);
}

// Returns the first occurrence of each row, in the order of rows.
template <typename Tup>
[[nodiscard]] Rows<Tup> firstOccurrences(const Rows<Tup>& rows) {
  Rows<Tup> out;
  out.reserve(rows.size());
  TupleIndex<Tup> seen{rows.size()};
  appendFirstOccurrences(out, seen, rows);
  return out;
}

// Appends each row of from that is in by if keep is true, or that is not in by if keep is false, in the order of
// from. A set holds no row twice, so a row is not checked against the rows already appended.
template <typename Tup>
void appendFiltered(Rows<Tup>& out, const SetFor<Tup>& from, const SetFor<Tup>& by, bool keep) {
  const auto& from_rows = SetAccess::rows(from);
  const auto& by_rows   = SetAccess::rows(by);
  const auto index      = indexRows(by_rows);
  ::dreamor::internal::hashedBlocks(
      0,
      from_rows.size(),
      rowsAt(from_rows),
      [&](std::size_t row, std::size_t hash) {
        if ((index.find(from_rows[row], hash, rowsAt(by_rows)) != TupleIndex<Tup>::kNotFound) == keep) {
          out.push_back(from_rows[row]);
        }
        return true;
      },
      index);
}

// Returns true if Result is a column or a non-empty tuple of columns.
template <typename Result>
[[nodiscard]] consteval bool isSelectedRow() {
  if constexpr (column<Result>) {
    return true;
  } else if constexpr (isIndexSetRow<Result>()) {
    return std::tuple_size_v<Result> > 0;
  } else {
    return false;
  }
}

// The row type that select(expr) builds: what expr returns, with a single column made into a row of one column. The
// type is computed only if checkColumnCallable accepts the callable, so that checkColumnCallable reports the only
// error for a callable that it rejects.
template <typename Expr,
          typename Tup,
          bool = ColumnsOfPredicate<Expr>::known && PlacementOf<Expr, Tup>::all_named &&
                 PlacementOf<Expr, Tup>::all_present>
struct SelectedRow {
  static constexpr bool known  = false;
  static constexpr bool is_row = true;
  using type                   = std::tuple<>;
};

template <typename Expr, typename Tup>
struct SelectedRow<Expr, Tup, true> {
  using Result = std::remove_cvref_t<decltype(callColumnsOf(std::declval<const Expr&>(), std::declval<const Tup&>()))>;

  static constexpr bool known  = true;
  static constexpr bool is_row = isSelectedRow<Result>();
  using type                   = std::conditional_t<column<Result>, std::tuple<Result>, Result>;
};

// Checks whether every row of left is in right. Only right is indexed, as in appendFiltered. If left and right are
// copies of one set, they share their rows, and the answer is true without an index.
template <typename Set>
[[nodiscard]] bool everyRowIn(const Set& left, const Set& right) {
  using Tup              = Set::value_type;
  const auto& left_rows  = SetAccess::rows(left);
  const auto& right_rows = SetAccess::rows(right);
  if (left_rows.data() == right_rows.data()) { return true; }
  const auto index    = indexRows(right_rows);
  const auto at       = rowsAt(right_rows);
  const auto in_right = [&](const Tup& row) { return index.find(row, at) != TupleIndex<Tup>::kNotFound; };
  return std::ranges::all_of(left_rows, in_right);
}

// Returns the rows of the transitive closure of edges, in three steps:
// - Each node gets one number, in the order in which the edges first name it.
// - The edges are grouped by their source, in the order of edges (compressed sparse rows, CSR).
// - For each source, in the order in which edges first lists it as a source, a breadth-first walk follows the edges.
//   The walk adds each node that one or more edges lead to, nearest first. If the walk meets a node again, by another
//   path or around a cycle, it does not add the node again, so two paths to one node give one row.
// The source itself is a row only if a cycle leads back to it. Each walk marks the nodes that it meets with the
// number of its source, so no mark needs to be cleared between walks.
template <typename From, typename To>
[[nodiscard]] Rows<std::tuple<From, To>> closureRows(const Rows<std::tuple<From, To>>& edges) {
  using Node = std::tuple<From>;
  Rows<Node> nodes;
  TupleIndex<Node> index{edges.size()};
  const auto node_at = rowsAt(nodes);
  const auto number  = [&](const From& node) {
    const std::size_t held = index.insert(Node{node}, nodes.size(), node_at);
    if (held != TupleIndex<Node>::kNotFound) { return held; }
    nodes.emplace_back(node);
    return nodes.size() - 1;
  };
  std::vector<std::size_t> source_of(edges.size());
  std::vector<std::size_t> target_of(edges.size());
  for (std::size_t edge = 0; edge < edges.size(); ++edge) {
    source_of[edge] = number(std::get<From>(edges[edge]));
    target_of[edge] = number(From{std::get<To>(edges[edge])});
  }

  const std::size_t count = nodes.size();
  // The targets of node n are targets[first[n]] to targets[first[n + 1] - 1].
  std::vector<std::size_t> first(count + 1, 0);
  for (const std::size_t source : source_of) {
    ++first[source + 1];
  }
  for (std::size_t n = 0; n < count; ++n) {
    first[n + 1] += first[n];
  }
  std::vector<std::size_t> targets(edges.size());
  std::vector<std::size_t> placed(first.begin(), first.end() - 1);
  std::vector<std::size_t> sources;  // In the order in which the relation first lists each node as a source.
  for (std::size_t edge = 0; edge < edges.size(); ++edge) {
    const std::size_t source = source_of[edge];
    if (placed[source] == first[source]) { sources.push_back(source); }
    targets[placed[source]++] = target_of[edge];
  }

  Rows<std::tuple<From, To>> out;
  out.reserve(edges.size());
  constexpr auto kUnmet = static_cast<std::size_t>(-1);
  std::vector<std::size_t> met_from(count, kUnmet);  // The source whose walk met each node last.
  std::vector<std::size_t> queue;
  queue.reserve(count);
  for (const std::size_t source : sources) {
    const From& from = std::get<0>(nodes[source]);
    queue.clear();
    queue.push_back(source);
    for (std::size_t next = 0; next < queue.size(); ++next) {
      const std::size_t node = queue[next];
      for (std::size_t edge = first[node]; edge < first[node + 1]; ++edge) {
        const std::size_t target = targets[edge];
        if (met_from[target] == source) { continue; }
        met_from[target] = source;
        if (target != source) { queue.push_back(target); }
        out.emplace_back(from, To{std::get<0>(nodes[target])});
      }
    }
  }
  return out;
}

template <typename T, typename... Ts>
inline constexpr bool kOneOf = (std::is_same_v<T, Ts> || ...);

// True if two sets have the same columns in another order.
template <typename Left, typename Right>
inline constexpr bool kReordered = false;
template <typename... Left, typename... Right>
inline constexpr bool kReordered<std::tuple<Left...>, std::tuple<Right...>> =
    sizeof...(Left) == sizeof...(Right) && !std::is_same_v<std::tuple<Left...>, std::tuple<Right...>> &&
    (kOneOf<Left, Right...> && ...);

// Returns the rows of set with its columns in another order, matched by name as select<...>() matches them. A set
// holds no row twice, and reordering the columns keeps distinct rows distinct, so no row needs to be dropped.
template <typename... Left, typename... Right>
[[nodiscard]] IndexSet<Left...> inColumnOrder(const IndexSet<Right...>& set) {
  return SetAccess::make(pickColumns<Left...>(set));
}

// True if two sets do not have the same columns, in any order. No set operation accepts such sets.
template <typename Left, typename Right>
inline constexpr bool kOtherColumns = !std::is_same_v<Left, Right> && !kReordered<Left, Right>;

}  // namespace dreamor::internal::inline index_set

namespace dreamor {

/// @section Membership
///
/// A `Membership` is a condition that keeps the rows of a set, of a chain or of a sum that are in another set, or
/// that are not in it. `IndexSet::membership` returns one, and `!` returns the opposite condition.
///
/// Guide: guide/deriving-sets.md

/// A condition that checks whether a row is in a set.
///
/// `IndexSet::membership` returns a `Membership`. A `where` of a chain, of a set, of set-builder notation or of a sum
/// takes a `Membership` as a condition. The condition reads the columns of the set, so the rows that it checks must
/// have those columns. In a chain, `build()` runs the condition as soon as every column of the set is bound. A
/// condition on a set of fewer columns can therefore run earlier and drop rows sooner.
///
/// A `Membership` holds a copy of the set, which shares the rows. The rows stay valid while the `Membership` exists.
///
/// `In` is true for the condition that `membership()` returns. `In` is false for the opposite condition, which `!`
/// returns. Like `NOT IN` in SQL, the opposite condition is true for the rows that are not in the set.
template <typename Set, bool In>
class Membership {
  using Tup = Set::value_type;

 public:
  /// Constructs a condition that checks whether a row is in `set`.
  ///
  /// `IndexSet::membership` calls this constructor.
  ///
  /// Cost: If no copy of `set` has built an index of its rows yet, the constructor builds one. The index stores
  /// positions of rows, not copies of them. `contains` and every `Membership`, on any copy of `set`, use the same
  /// index.
  explicit Membership(Set set) : set_{std::move(set)}, index_{&internal::sharedIndex(set_)} {}

  /// Checks whether the row that `key` identifies is in the set, or, if `In` is false, whether it is not.
  ///
  /// Takes: `key` must be a key of the set, as the Keys section describes.
  /// Returns: If `In` is true, true if the set has the row; false otherwise. If `In` is false, true if the set does
  /// not have the row; false otherwise.
  template <typename... Args>
  [[nodiscard]] bool operator()(const Args&... key) const {
    // where() cannot read the columns from this parameter list, so the specialization of CallableColumns below
    // declares them.
    if constexpr (internal::checkMembershipKey<Tup, Args...>()) {
      const auto row     = internal::keyOf<Tup>(key...);
      const bool present = row && index_->find(*row, internal::rowsAt(internal::SetAccess::rows(set_))) !=
                                      internal::TupleIndex<Tup>::kNotFound;
      return present == In;
    } else {
      return false;
    }
  }

  /// Returns the opposite condition.
  ///
  /// If this condition checks whether a row is in the set, the opposite checks whether a row is not in the set, like
  /// `NOT IN` in SQL. The opposite of the opposite is the original condition. The new `Membership` shares the set
  /// and its index with this one.
  [[nodiscard]] Membership<Set, !In> operator!() const { return Membership<Set, !In>{set_, index_}; }

 private:
  template <typename, bool>
  friend class Membership;

  Membership(Set set, const internal::TupleIndex<Tup>* index) : set_{std::move(set)}, index_{index} {}

  Set set_;                                 // Keeps the rows, and with them the index, alive.
  const internal::TupleIndex<Tup>* index_;  // The shared index of the set.
};

template <typename... Columns>
template <typename... Picked>
IndexSet<Picked...> IndexSet<Columns...>::select() const {
  static_assert(sizeof...(Picked) > 0, "select<Columns...>() names the columns it keeps: S.select<cMachine, cItem>().");
  internal::checkColumns<Tup, Picked...>();
  return internal::SetAccess::make(internal::firstOccurrences(internal::pickColumns<Picked...>(*this)));
}

// A value moves into another column only if expr names that column. cTo does not convert to cFrom implicitly, so
// two columns of one value type cannot swap their values by mistake.
template <typename... Columns>
template <typename Expr>
auto IndexSet<Columns...>::select(Expr expr) const {
  internal::checkColumnCallable<Expr, Tup>();
  using Selected = internal::SelectedRow<Expr, Tup>;
  using NewRow   = Selected::type;
  static_assert(Selected::is_row,
                "select(expr) returns the new row's columns: one Col, or a std::tuple of them - "
                "`return std::tuple<cFrom, cTo>{j, i};`. A bare value does not say which column it fills.");
  static_assert(
      !Selected::is_row || internal::allColumnsDistinct<NewRow>(std::make_index_sequence<std::tuple_size_v<NewRow>>{}),
      "select(expr) returns the same column twice. Each column of the new row needs its own type.");
  if constexpr (Selected::known && Selected::is_row) {
    internal::Rows<NewRow> out;
    out.reserve(size());
    for (const auto& row : *this) {
      out.emplace_back(internal::callColumnsOf(expr, row));
    }
    return internal::SetAccess::make(internal::firstOccurrences(out));
  } else {
    return internal::SetAccess::empty<NewRow>();
  }
}

template <typename... Columns>
Membership<IndexSet<Columns...>> IndexSet<Columns...>::membership() const {
  return Membership<IndexSet>{*this};
}

template <typename... Columns>
template <typename... Args>
bool IndexSet<Columns...>::contains(const Args&... key) const {
  if constexpr (internal::checkMembershipKey<Tup, Args...>()) {
    const auto row = internal::keyOf<Tup>(key...);
    if (!row) { return false; }
    return internal::sharedIndex(*this).find(*row, internal::rowsAt(internal::SetAccess::rows(*this))) !=
           internal::TupleIndex<Tup>::kNotFound;
  } else {
    return false;
  }
}

template <typename... Columns>
template <typename Key, typename Direction>
IndexSet<Columns...> IndexSet<Columns...>::orderBy(Key key, Direction /*direction*/) const {
  internal::checkColumnCallable<Key, Tup>();
  static_assert(std::is_same_v<Direction, Ascending> || std::is_same_v<Direction, Descending>,
                "orderBy(key, direction): the direction is dreamor::ascending or dreamor::descending.");
  if constexpr (internal::ColumnsOfPredicate<Key>::known && internal::PlacementOf<Key, Tup>::all_named &&
                internal::PlacementOf<Key, Tup>::all_present) {
    using Compare = std::conditional_t<std::is_same_v<Direction, Descending>, std::ranges::greater, std::ranges::less>;
    const auto key_of = [&key](const Tup& row) { return internal::callColumnsOf(key, row); };
    internal::Rows<Tup> rows{begin(), end()};
    std::ranges::stable_sort(rows, Compare{}, key_of);
    return internal::SetAccess::make(std::move(rows));
  } else {
    return internal::SetAccess::empty<Tup>();
  }
}

/// @section Set algebra
///
/// The set algebra combines and compares two whole sets. `unite`, `intersect` and `difference` return a new set, and
/// `operator==` and `within` return a `bool`. Neither operand changes.
///
/// The two sets must have the same columns. The right operand may have them in another order. Its rows are then
/// matched to the rows of the left operand by column name, and a new set has the columns of the left operand, in
/// their order. Two sets with different columns do not compile. To keep or drop the rows of a set that are in a set
/// of fewer columns, use `membership()` as a condition of `where`.
///
/// The rows of a new set are in the order in which they first appear: the rows of the left operand first, then
/// those of the right operand. dreamor finds rows through a hash index, but the index decides only whether a row is
/// in a set, never the order of the rows. So the result is the same in every run.
///
/// Guide: guide/set-algebra.md

/// Returns the union of `left` and `right`: the rows that are in `left`, in `right`, or in both.
///
/// Takes: `left` and `right` must be sets of the same columns, in the same order. For the same columns in another
/// order, use the overload below.
/// Returns: The rows of `left` in their order, then the rows of `right` that `left` does not have, in their order.
/// Cost: `O(|left| + |right|)`, one index over `left`.
template <typename... Columns>
[[nodiscard]] IndexSet<Columns...> unite(const IndexSet<Columns...>& left, const IndexSet<Columns...>& right) {
  // Neither set holds a row twice, so only left needs an index.
  const auto& left_rows = internal::SetAccess::rows(left);
  internal::Rows<std::tuple<Columns...>> out;
  out.reserve(left.size() + right.size());
  out.insert(out.end(), left_rows.begin(), left_rows.end());
  internal::appendFiltered(out, right, left, false);
  return internal::SetAccess::make(std::move(out));
}

/// @overload
/// Returns the union of `left` and `right`, where `right` has the columns of `left` in another order.
///
/// The rows of `right` are matched to the rows of `left` by column name. The new set has the columns of `left`, in
/// their order, and holds the rows that are in `left`, in `right`, or in both.
///
/// Takes: `right` must have the same columns as `left`, in another order.
/// Returns: The rows of `left` in their order, then the rows of `right` that `left` does not have, in their order.
/// Cost: `O(|left| + |right|)`. The rows of `right` are copied with their columns in the order of `left`, then one
/// index is built over `left`.
template <typename... Left, typename... Right>
  requires internal::kReordered<std::tuple<Left...>, std::tuple<Right...>>
[[nodiscard]] IndexSet<Left...> unite(const IndexSet<Left...>& left, const IndexSet<Right...>& right) {
  return unite(left, internal::inColumnOrder<Left...>(right));
}

/// @hidden - exists only to reject two sets of different columns with a message
template <typename... Left, typename... Right>
  requires internal::kOtherColumns<std::tuple<Left...>, std::tuple<Right...>>
IndexSet<Left...> unite(const IndexSet<Left...>& left, const IndexSet<Right...>& /*right*/) {
  static_assert(internal::always_false<Right...>, DREAMOR_INTERNAL_SAME_COLUMNS_MESSAGE);
  return left;
}

/// Returns the intersection of `left` and `right`: the rows of `left` that are also in `right`.
///
/// Takes: `left` and `right` must be sets of the same columns, in the same order. For the same columns in another
/// order, use the overload below.
/// Returns: The rows are in the order of `left`.
/// Cost: `O(|left| + |right|)`, one index over `right`.
template <typename... Columns>
[[nodiscard]] IndexSet<Columns...> intersect(const IndexSet<Columns...>& left, const IndexSet<Columns...>& right) {
  internal::Rows<std::tuple<Columns...>> out;
  internal::appendFiltered(out, left, right, true);
  return internal::SetAccess::make(std::move(out));
}

/// @overload
/// Returns the intersection of `left` and `right`, where `right` has the columns of `left` in another order.
///
/// The rows of `right` are matched to the rows of `left` by column name. The new set has the columns of `left`, in
/// their order, and holds the rows of `left` that are also in `right`.
///
/// Takes: `right` must have the same columns as `left`, in another order.
/// Returns: The rows are in the order of `left`.
/// Cost: `O(|left| + |right|)`. The rows of `right` are copied with their columns in the order of `left`, then one
/// index is built over `right`.
template <typename... Left, typename... Right>
  requires internal::kReordered<std::tuple<Left...>, std::tuple<Right...>>
[[nodiscard]] IndexSet<Left...> intersect(const IndexSet<Left...>& left, const IndexSet<Right...>& right) {
  return intersect(left, internal::inColumnOrder<Left...>(right));
}

/// @hidden - exists only to reject two sets of different columns with a message
template <typename... Left, typename... Right>
  requires internal::kOtherColumns<std::tuple<Left...>, std::tuple<Right...>>
IndexSet<Left...> intersect(const IndexSet<Left...>& left, const IndexSet<Right...>& /*right*/) {
  static_assert(internal::always_false<Right...>, DREAMOR_INTERNAL_SAME_COLUMNS_MESSAGE);
  return left;
}

/// Returns the difference of `left` and `right`: the rows of `left` that are not in `right`.
///
/// Takes: `left` and `right` must be sets of the same columns, in the same order. For the same columns in another
/// order, use the overload below.
/// Returns: The rows are in the order of `left`.
/// Cost: `O(|left| + |right|)`, one index over `right`.
template <typename... Columns>
[[nodiscard]] IndexSet<Columns...> difference(const IndexSet<Columns...>& left, const IndexSet<Columns...>& right) {
  internal::Rows<std::tuple<Columns...>> out;
  internal::appendFiltered(out, left, right, false);
  return internal::SetAccess::make(std::move(out));
}

/// @overload
/// Returns the difference of `left` and `right`, where `right` has the columns of `left` in another order.
///
/// The rows of `right` are matched to the rows of `left` by column name. The new set has the columns of `left`, in
/// their order, and holds the rows of `left` that are not in `right`.
///
/// Takes: `right` must have the same columns as `left`, in another order.
/// Returns: The rows are in the order of `left`.
/// Cost: `O(|left| + |right|)`. The rows of `right` are copied with their columns in the order of `left`, then one
/// index is built over `right`.
template <typename... Left, typename... Right>
  requires internal::kReordered<std::tuple<Left...>, std::tuple<Right...>>
[[nodiscard]] IndexSet<Left...> difference(const IndexSet<Left...>& left, const IndexSet<Right...>& right) {
  return difference(left, internal::inColumnOrder<Left...>(right));
}

/// @hidden - exists only to reject two sets of different columns with a message
template <typename... Left, typename... Right>
  requires internal::kOtherColumns<std::tuple<Left...>, std::tuple<Right...>>
IndexSet<Left...> difference(const IndexSet<Left...>& left, const IndexSet<Right...>& /*right*/) {
  static_assert(internal::always_false<Right...>, DREAMOR_INTERNAL_SAME_COLUMNS_MESSAGE);
  return left;
}

/// Checks whether `left` and `right` hold the same rows.
///
/// The order of the rows is not compared. This is the equality of sets, as the `==` of `std::unordered_set` is. To
/// compare the order of the rows too, use `std::ranges::equal`.
///
/// Takes: `left` and `right` must be sets of the same columns, in the same order. For the same columns in another
/// order, use the overload below.
/// Returns: True if the two sets hold the same rows; false otherwise.
/// Cost: `O(|left| + |right|)`, one index over `right`. Two copies of one set compare equal without an index.
template <typename... Columns>
[[nodiscard]] bool operator==(const IndexSet<Columns...>& left, const IndexSet<Columns...>& right) {
  // Neither set holds a row twice. So if the sizes are equal and every row of left is in right, the sets are equal.
  return left.size() == right.size() && internal::everyRowIn(left, right);
}

/// @overload
/// Checks whether `left` and `right` hold the same rows, where `right` has the columns of `left` in another order.
///
/// The rows of `right` are matched to the rows of `left` by column name. The order of the rows is not compared.
///
/// Takes: `right` must have the same columns as `left`, in another order.
/// Returns: True if the two sets hold the same rows; false otherwise.
/// Cost: `O(|left| + |right|)`. If the sets have the same number of rows, the rows of `right` are copied with their
/// columns in the order of `left`, then one index is built over the copy.
template <typename... Left, typename... Right>
  requires internal::kReordered<std::tuple<Left...>, std::tuple<Right...>>
[[nodiscard]] bool operator==(const IndexSet<Left...>& left, const IndexSet<Right...>& right) {
  return left.size() == right.size() && left == internal::inColumnOrder<Left...>(right);
}

/// Checks whether every row of `inner` is in `outer`.
///
/// A set is within itself. The empty set is within every set of the same columns.
///
/// Takes: `inner` and `outer` must be sets of the same columns, in the same order. For the same columns in another
/// order, use the overload below.
/// Returns: True if every row of `inner` is in `outer`; false otherwise.
/// Cost: `O(|inner| + |outer|)`, one index over `outer`. A copy of a set is within the set without an index.
template <typename... Columns>
[[nodiscard]] bool within(const IndexSet<Columns...>& inner, const IndexSet<Columns...>& outer) {
  return inner.size() <= outer.size() && internal::everyRowIn(inner, outer);
}

/// @overload
/// Checks whether every row of `inner` is in `outer`, where `outer` has the columns of `inner` in another order.
///
/// The rows of `outer` are matched to the rows of `inner` by column name.
///
/// Takes: `outer` must have the same columns as `inner`, in another order.
/// Returns: True if every row of `inner` is in `outer`; false otherwise.
/// Cost: `O(|inner| + |outer|)`. If `inner` has no more rows than `outer`, the rows of `outer` are copied with their
/// columns in the order of `inner`, then one index is built over the copy.
template <typename... Left, typename... Right>
  requires internal::kReordered<std::tuple<Left...>, std::tuple<Right...>>
[[nodiscard]] bool within(const IndexSet<Left...>& inner, const IndexSet<Right...>& outer) {
  return inner.size() <= outer.size() && within(inner, internal::inColumnOrder<Left...>(outer));
}

/// @hidden - exists only to reject two sets of different columns with a message
template <typename... Left, typename... Right>
  requires internal::kOtherColumns<std::tuple<Left...>, std::tuple<Right...>>
bool within(const IndexSet<Left...>& /*inner*/, const IndexSet<Right...>& /*outer*/) {
  static_assert(internal::always_false<Right...>, DREAMOR_INTERNAL_SAME_COLUMNS_MESSAGE);
  return false;
}

/// Returns the transitive closure of `relation`: every pair `(a, b)` such that a path of one or more edges leads
/// from `a` to `b`. Each pair appears once, however many paths connect them.
///
/// Each row of `relation` is an edge from the node in its first column to the node in its second column. Cycles are
/// permitted: a node on a cycle is paired with itself. A node that is on no cycle is not paired with itself. The
/// reflexive transitive closure also pairs every node with itself. To get it, unite the result with a set that pairs
/// each node with itself.
///
/// Takes: `relation` must be a set of two columns. The two columns must hold the same value type, such as `cParent`
/// and `cChild`, both entity columns of `Item`.
/// Returns: The rows are grouped by `a`, in the order in which `relation` first lists each `a` in its first column.
/// Within a group, a `b` that fewer edges separate from `a` comes first, because the closure is found by a
/// breadth-first walk from `a`.
/// Cost: `O(V (V + E))`, where `V` is the number of nodes and `E` is the number of rows of `relation`.
template <typename From, typename To>
[[nodiscard]] IndexSet<From, To> transitiveClosure(const IndexSet<From, To>& relation) {
  constexpr bool kOneKind = std::is_same_v<typename From::value_type, typename To::value_type>;
  static_assert(kOneKind,
                "transitiveClosure takes a relation from a node to a node of the same kind - IndexSet<cParent, "
                "cChild>, both columns over Item: a path leads from a node only to a node of its own kind.");
  if constexpr (kOneKind) {
    return internal::SetAccess::make(internal::closureRows<From, To>(internal::SetAccess::rows(relation)));
  } else {
    return relation;
  }
}

/// @hidden - exists only to reject a relation that does not have two columns, with a message
template <typename... Columns>
  requires(sizeof...(Columns) != 2)
IndexSet<Columns...> transitiveClosure(const IndexSet<Columns...>& relation) {
  static_assert(internal::always_false<Columns...>,
                "transitiveClosure takes a relation of two columns, from a node to a node - IndexSet<cParent, "
                "cChild>: pick them from a wider set first, S.select<cParent, cChild>().");
  return relation;
}

}  // namespace dreamor

// Declares the columns of a Membership, so that where() places the condition by them. operator() accepts any
// arguments of the right number, so where() cannot read the columns from its signature.
template <typename Set, bool In>
struct dreamor::internal::CallableColumns<dreamor::Membership<Set, In>> {
  using type = Set::value_type;
};
