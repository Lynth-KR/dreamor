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
// IndexSet, defined here: orderBy, select, membership.
#pragma once

#include <algorithm>  // IWYU pragma: keep // For std::ranges::stable_sort
#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"
#include "dreamor/index_set/binding.hpp"
#include "dreamor/index_set/call.hpp"
#include "dreamor/index_set/column.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"

namespace dreamor {

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

template <typename Tup>
using TupleIndex = ::dreamor::general::FlatIndex<Tup>;

// The key_at of a FlatIndex whose entries are positions in rows.
template <typename Tup>
[[nodiscard]] auto rowsAt(const std::vector<Tup>& rows) {
  return [&rows](std::size_t row) -> const Tup& { return rows[row]; };
}

// Every row by position; a repeat keeps its first one. Rows are hashed a block ahead
// (general::hashedBlocks), as everywhere below that walks a whole set.
template <typename Tup>
[[nodiscard]] TupleIndex<Tup> indexRows(const std::vector<Tup>& rows) {
  TupleIndex<Tup> index{rows.size()};
  const auto at = rowsAt(rows);
  ::dreamor::general::hashedBlocks(
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
void appendNew(std::vector<Tup>& out, TupleIndex<Tup>& seen, const Tup& tuple, std::size_t hash) {
  if (seen.insert(tuple, hash, out.size(), rowsAt(out)) == TupleIndex<Tup>::kNotFound) {
    out.push_back(tuple);
  }
}

// Appends the first occurrence of each of rows that out does not have yet, front to back.
template <typename Tup>
void appendFirstOccurrences(std::vector<Tup>& out, TupleIndex<Tup>& seen, const std::vector<Tup>& rows) {
  ::dreamor::general::hashedBlocks(
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
[[nodiscard]] std::vector<Tup> firstOccurrences(const std::vector<Tup>& rows) {
  std::vector<Tup> out;
  out.reserve(rows.size());
  TupleIndex<Tup> seen{rows.size()};
  appendFirstOccurrences(out, seen, rows);
  return out;
}

// The first occurrence of each row of left that is in right (keep) or is not (!keep).
template <typename Tup>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what filtering one by the other means
[[nodiscard]] std::vector<Tup> filterAgainst(const IndexSet<Tup>& left, const IndexSet<Tup>& right, bool keep) {
  const auto& left_rows  = SetAccess::rows(left);
  const auto& right_rows = SetAccess::rows(right);
  const auto rhs         = indexRows(right_rows);
  std::vector<Tup> out;
  TupleIndex<Tup> emitted{left_rows.size()};
  ::dreamor::general::hashedBlocks(
      0,
      left_rows.size(),
      rowsAt(left_rows),
      [&](std::size_t row, std::size_t hash) {
        if ((rhs.find(left_rows[row], hash, rowsAt(right_rows)) != TupleIndex<Tup>::kNotFound) == keep) {
          appendNew(out, emitted, left_rows[row], hash);
        }
        return true;
      },
      rhs,
      emitted);
  return out;
}

}  // namespace internal

// Projection onto some columns, as a true set: S.select<MachineCol, ItemCol>(). Naming every
// column in another order reorders the columns: S.select<OrderCol, MachineCol, ItemCol>().
template <typename Tup>
template <typename... Columns>
IndexSet<std::tuple<Columns...>> IndexSet<Tup>::select() const {
  static_assert(sizeof...(Columns) > 0, "select() needs at least one column.");
  internal::checkColumns<Tup, Columns...>();
  return internal::SetAccess::make(internal::firstOccurrences(internal::pickColumns<Columns...>(*this)));
}

namespace internal {

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

}  // namespace internal

// The general setof: each row becomes what expr returns - AMPL's setof {(i,j) in EDGES} (j,i).
// expr takes the columns it reads, as a predicate does, and returns the new row's columns:
//
//     sEDGES.select([](FromCol i, ToCol j) { return std::tuple<FromCol, ToCol>{j, i}; })   // reversed edges
//     sNODES.select([](NodeCol n) { return FromCol{n}; })                                  // a row of one
//
// A value moves into another column only where that column is written out: ToCol does not
// convert to FromCol on its own, so two columns over one value type cannot trade places
// unnoticed. As a set, like select<Cols...>(): first occurrence of each row.
template <typename Tup>
template <typename Expr>
auto IndexSet<Tup>::select(Expr expr) const {
  internal::checkColumnCallable<Expr, Tup>();
  using Selected = internal::SelectedRow<Expr, Tup>;
  using NewRow   = Selected::type;
  static_assert(Selected::is_row,
                "select(expr) returns the new row's columns: one Col, or a std::tuple of them - "
                "`return std::tuple<FromCol, ToCol>{j, i};`. A bare value does not say which column it fills.");
  static_assert(
      !Selected::is_row || internal::allColumnsDistinct<NewRow>(std::make_index_sequence<std::tuple_size_v<NewRow>>{}),
      "select(expr) returns the same column twice. Each column of the new row needs its own type.");
  static_assert(!Selected::is_row ||
                    internal::allColumnNamesDistinct<NewRow>(std::make_index_sequence<std::tuple_size_v<NewRow>>{}),
                "select(expr) returns two columns with the same name. Names have to be unique even when the value "
                "types differ, because kName is what names a variable in the model.");
  if constexpr (Selected::known && Selected::is_row) {
    std::vector<NewRow> out;
    out.reserve(size());
    for (const auto& row : *this) {
      out.emplace_back(internal::callColumnsOf(expr, row));
    }
    return internal::SetAccess::make(internal::firstOccurrences(out));
  } else {
    return IndexSet<NewRow>{};
  }
}

// A ∪ B
template <typename Tup>
[[nodiscard]] IndexSet<Tup> unite(const IndexSet<Tup>& left, const IndexSet<Tup>& right) {
  std::vector<Tup> out;
  out.reserve(left.size() + right.size());
  internal::TupleIndex<Tup> seen{left.size() + right.size()};
  internal::appendFirstOccurrences(out, seen, internal::SetAccess::rows(left));
  internal::appendFirstOccurrences(out, seen, internal::SetAccess::rows(right));
  return internal::SetAccess::make(std::move(out));
}

// A ∩ B
template <typename Tup>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what intersecting means
[[nodiscard]] IndexSet<Tup> intersect(const IndexSet<Tup>& left, const IndexSet<Tup>& right) {
  return internal::SetAccess::make(internal::filterAgainst(left, right, true));
}

// A \ B
template <typename Tup>
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): two sets of one type is what A \ B means
[[nodiscard]] IndexSet<Tup> difference(const IndexSet<Tup>& left, const IndexSet<Tup>& right) {
  return internal::SetAccess::make(internal::filterAgainst(left, right, false));
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
  // Shares the set's rows and indexes them in place: 4 bytes a slot, no copy of a row.
  explicit Membership(const IndexSet<Tup>& set)
      : set_{set}, index_{internal::indexRows(internal::SetAccess::rows(set))} {}

  // The set's columns, in order - where() calls pred(a, b), and so can you: in_routes(m, i) -
  // or a whole row (see key_args.hpp). The parameter list cannot say which columns these are
  // to where(); PredicateColumns below does that.
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
  IndexSet<Tup> set_;
  internal::TupleIndex<Tup> index_;
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
