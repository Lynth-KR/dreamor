// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// File: algebra.hpp
// Set algebra over materialized index sets.
//
// Contract: every operation preserves first-appearance order of the left operand, then of
// the right. Hashing decides membership only, never output order, so results are
// reproducible even when tuples hold pointers.
//
// Every operation returns a new IndexSet and leaves its operands as they were. Binary
// operations are free functions (their operands are symmetric); unary ones are members of
// IndexSet, defined here: orderBy, select, membership.
#pragma once

#include <algorithm>  // IWYU pragma: keep // For std::ranges::stable_sort
#include <cstddef>
#include <functional>  // IWYU pragma: keep // For std::ranges::less and greater
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

namespace dreamor {

namespace internal::inline index_set {

template <typename... Columns, typename Set>
[[nodiscard]] auto pickColumns(const Set& set) {
  Rows<std::tuple<Columns...>> out;
  out.reserve(set.size());
  for (const auto& tuple : set) {
    out.emplace_back(std::get<Columns>(tuple)...);
  }
  return out;
}

}  // namespace internal::inline index_set

namespace internal::inline index_set {

template <typename Tup>
using TupleIndex = ::dreamor::internal::FlatIndex<Tup>;

// The key_at of a FlatIndex whose entries are positions in rows.
template <typename Tup>
[[nodiscard]] auto rowsAt(const Rows<Tup>& rows) {
  return [&rows](std::size_t row) -> const Tup& { return rows[row]; };
}

// Every row by position; a repeat keeps its first one. Rows are hashed a block ahead
// (hashedBlocks), as everywhere below that walks a whole set.
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

// Appends tuple, whose hash this is, unless out has it already; seen indexes out.
template <typename Tup>
void appendNew(Rows<Tup>& out, TupleIndex<Tup>& seen, const Tup& tuple, std::size_t hash) {
  if (seen.insert(tuple, hash, out.size(), rowsAt(out)) == TupleIndex<Tup>::kNotFound) { out.push_back(tuple); }
}

// Appends the first occurrence of each of rows that out does not have yet, front to back.
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

// First occurrence of each tuple, front to back.
template <typename Tup>
[[nodiscard]] Rows<Tup> firstOccurrences(const Rows<Tup>& rows) {
  Rows<Tup> out;
  out.reserve(rows.size());
  TupleIndex<Tup> seen{rows.size()};
  appendFirstOccurrences(out, seen, rows);
  return out;
}

// Appends each row of `from` that is in `by` (keep) or is not (!keep), in from's order. A set holds
// no row twice, so no row needs to be checked against the ones already kept.
template <typename Tup>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what filtering one by the other means
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

}  // namespace internal::inline index_set

// Projection onto some columns, as a true set: S.select<cMachine, cItem>(). Naming every
// column in another order reorders the columns: S.select<cOrder, cMachine, cItem>().
template <typename... Columns>
template <typename... Picked>
IndexSet<Picked...> IndexSet<Columns...>::select() const {
  static_assert(sizeof...(Picked) > 0, "select() needs at least one column.");
  internal::checkColumns<Tup, Picked...>();
  return internal::SetAccess::make(internal::firstOccurrences(internal::pickColumns<Picked...>(*this)));
}

namespace internal::inline index_set {

// A Col, or a non-empty tuple of Cols.
template <typename Result>
[[nodiscard]] consteval bool isSelectedRow() {
  if constexpr (is_column<Result>) {
    return true;
  } else if constexpr (isIndexSetRow<Result>()) {
    return std::tuple_size_v<Result> > 0;
  } else {
    return false;
  }
}

// The row select(expr) builds: what expr returns, a lone column standing for a row of one.
// Probed only once the callable itself is readable, so checkColumnCallable stays the only
// diagnostic for a callable that is not.
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
  using type                   = std::conditional_t<is_column<Result>, std::tuple<Result>, Result>;
};

}  // namespace internal::inline index_set

// The general setof: each row becomes what expr returns - AMPL's setof {(i,j) in EDGES} (j,i).
// expr takes the columns it reads, as a predicate does, and returns the new row's columns:
//
//     sEDGES.select([](cFrom i, cTo j) { return std::tuple<cFrom, cTo>{j, i}; })   // reversed edges
//     sNODES.select([](cNode n) { return cFrom{n}; })                                  // a row of one
//
// A value moves into another column only where that column is written out: cTo does not
// convert to cFrom on its own, so two columns over one value type cannot trade places
// unnoticed. As a set, like select<Cols...>(): first occurrence of each row.
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
  static_assert(!Selected::is_row ||
                    internal::allColumnNamesDistinct<NewRow>(std::make_index_sequence<std::tuple_size_v<NewRow>>{}),
                "select(expr) returns two columns with the same name. Names have to be unique even when the value "
                "types differ, because kName is what names a variable in the model.");
  if constexpr (Selected::known && Selected::is_row) {
    internal::Rows<NewRow> out;
    out.reserve(size());
    for (const auto& row : *this) {
      out.emplace_back(internal::callColumnsOf(expr, row));
    }
    return internal::SetAccess::make(internal::firstOccurrences(out));
  } else {
    return internal::SetFor<NewRow>{};
  }
}

// A union B: A, then B diff A. Neither holds a row twice, so only A needs an index.
template <typename... Columns>
[[nodiscard]] IndexSet<Columns...> unite(const IndexSet<Columns...>& left, const IndexSet<Columns...>& right) {
  const auto& left_rows = internal::SetAccess::rows(left);
  internal::Rows<std::tuple<Columns...>> out;
  out.reserve(left.size() + right.size());
  out.insert(out.end(), left_rows.begin(), left_rows.end());
  internal::appendFiltered(out, right, left, false);
  return internal::SetAccess::make(std::move(out));
}

// A inter B
template <typename... Columns>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what intersecting means
[[nodiscard]] IndexSet<Columns...> intersect(const IndexSet<Columns...>& left, const IndexSet<Columns...>& right) {
  internal::Rows<std::tuple<Columns...>> out;
  internal::appendFiltered(out, left, right, true);
  return internal::SetAccess::make(std::move(out));
}

// A \ B
template <typename... Columns>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what A \ B means
[[nodiscard]] IndexSet<Columns...> difference(const IndexSet<Columns...>& left, const IndexSet<Columns...>& right) {
  internal::Rows<std::tuple<Columns...>> out;
  internal::appendFiltered(out, left, right, false);
  return internal::SetAccess::make(std::move(out));
}

namespace internal::inline index_set {

// Whether every row of left is in right - indexing right alone, as appendFiltered does. Copies of
// one set share its rows, and a set holds each of them.
template <typename Set>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what a subset test takes
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

}  // namespace internal::inline index_set

// A = B: the same rows, in any order - a set's equality, as std::unordered_set's. Neither holds a
// row twice, so equal sizes and every row of A in B say it. Whether the rows also come in the same
// order is std::ranges::equal(A, B).
template <typename... Columns>
[[nodiscard]] bool operator==(const IndexSet<Columns...>& left, const IndexSet<Columns...>& right) {
  return left.size() == right.size() && internal::everyRowIn(left, right);
}

// A within B: every row of A is in B - AMPL's within, a subset test. Read as written: within(sLATE,
// sORDERS) asks whether sLATE lies inside sORDERS. A set lies within itself, and the empty set within
// every set.
template <typename... Columns>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what A within B means
[[nodiscard]] bool within(const IndexSet<Columns...>& inner, const IndexSet<Columns...>& outer) {
  return inner.size() <= outer.size() && internal::everyRowIn(inner, outer);
}

// O(1) membership test, so one set can filter another during generation instead of
// intersecting afterwards:
//
//     const auto in_supply = sSUPPLY.membership();       // over (cNode, cPeriod)
//     makeSet<cNode, cPeriod>(nodes, periods).where(in_supply)
//
// A membership over fewer columns prunes earlier: one over (cNode) alone runs as soon as
// the node is bound.
template <typename Set>
class Membership {
  using Tup = Set::value_type;

 public:
  // Shares the set's rows and indexes them in place: 4 bytes a slot, no copy of a row.
  explicit Membership(const Set& set) : set_{set}, index_{internal::indexRows(internal::SetAccess::rows(set))} {}

  // The set's columns, in order - where() calls pred(a, b), and so can you: in_routes(m, i) -
  // or a whole row (see key_args.hpp). The parameter list cannot say which columns these are
  // to where(); CallableColumns below does that.
  template <typename... Args>
  [[nodiscard]] bool operator()(const Args&... key) const {
    if constexpr (internal::checkMembershipKey<Tup, Args...>()) {
      return index_.find(internal::keyOf<Tup>(key...), internal::rowsAt(internal::SetAccess::rows(set_))) !=
             internal::TupleIndex<Tup>::kNotFound;
    } else {
      return false;
    }
  }

 private:
  Set set_;
  internal::TupleIndex<Tup> index_;
};

// Declares this membership's columns, so where() places it by them rather than by probing
// its signature - which would match any call with the right number of arguments.
template <typename Set>
struct CallableColumns<Membership<Set>> {
  using type = Set::value_type;
};

template <typename... Columns>
Membership<IndexSet<Columns...>> IndexSet<Columns...>::membership() const {
  return Membership<IndexSet>{*this};
}

// Stable reordering by a key - SQL's ORDER BY: ascending, or descending with
// orderBy(key, descending). The key takes the columns it reads, as a predicate does, and returns
// what to compare. Pass an explicit key (an id, a name): a key that falls back on pointer
// comparison orders by address and varies between runs.
//
//     const auto name_of       = [](cItem i) { return i->name; };
//     const auto sBY_NAME      = sITEMS.orderBy(name_of);                       // ORDER BY name
//     const auto sBY_NAME_DOWN = sITEMS.orderBy(name_of, dreamor::descending);  // ORDER BY name DESC
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
    return IndexSet{};
  }
}

}  // namespace dreamor
