// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// File: indexed.hpp
// The materialized index set: what makeSet(...).collect() and every set operation return.
//
// Read-only. A set is a fact about the model - which index tuples exist - so code that
// receives one can walk it but not edit it:
//
//     for (const auto& [m, i] : sMACHINE_ITEMS) ...           // begin() / end()
//     std::ranges::count_if(sOPEN_ORDERS, is_large);        // any read-only algorithm
//     sMACHINE_ITEMS.size();
//
// To get a different set, derive one. Unary derivations are members, binary ones are free
// functions; each returns a new set and leaves S as it was:
//
//     S.where(p, q)              rows passing every predicate     {(m,i) in S: p and q}
//     S.orderBy(key)             stable reorder by key (descending: orderBy(key, descending))
//     S.select<cMachine, ...>()   projection (or column reorder), as a set
//     S.select(expr)             each row as expr makes it, as a set   setof {(i,j) in S} (j,i)
//     S.groupBy<cOrder>()       Grouped: walk groups, slice by key
//     S.membership()             O(1) membership predicate over S's columns
//     S.ordered()                ord / next / prev
//     S.ordered<cMachine>()    the same within each machine's rows   next(t, S[m])
//     unite(A, B), intersect(A, B), difference(A, B)
//
// The members are defined in the headers that implement them; include
// "dreamor/index_set.hpp" to have all of them.
//
// Rows are stored once, immutably, behind a shared pointer, so a copy is O(1) and shares
// the rows instead of duplicating them. Each row is a plain std::tuple of columns:
// structured bindings and std::get<Col> work on it directly.
#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/base/aligned_vector.hpp"

namespace dreamor {

template <typename... Columns>
class IndexSet;
template <typename Set, typename... Columns>
class Grouped;
template <typename Set, typename... Within>
class Ordered;
template <typename Set>
class Membership;

// The order orderBy sorts in: ascending, the default, or descending - SQL's ASC and DESC.
inline constexpr struct Ascending {
} ascending{};  // NOLINT(readability-identifier-naming): a public tag object, named like std::in_place
inline constexpr struct Descending {
} descending{};  // NOLINT(readability-identifier-naming): a public tag object, named like std::in_place

namespace internal::inline index_set {

// The set whose rows are Tup: IndexSet<Cs...> for std::tuple<Cs...>. Code that works on a set's rows
// computes a row type; this names the set of it.
template <typename Tup>
struct SetForTuple;
template <typename... Columns>
struct SetForTuple<std::tuple<Columns...>> {
  using type = IndexSet<Columns...>;
};
template <typename Tup>
using SetFor = SetForTuple<Tup>::type;

// Whether T is a set: what a source can share its rows with rather than copy them.
template <typename T>
inline constexpr bool is_index_set = false;
template <typename... Columns>
inline constexpr bool is_index_set<IndexSet<Columns...>> = true;

// One object built from a set's rows and shared by everything built on them - the row index
// every variable and value table on the set looks keys up in (internal/backend/row_index.hpp). Held
// weakly: it lives while something built on it does, and the next one after that builds it
// again, so a set alone keeps no index alive. The set layer does not know its type.
class SharedSlot {
 public:
  // What make() returns, made at most once while anything still holds it. make() runs under
  // the lock: a second table on the same set waits for the first one's index instead of
  // building its own. If make() throws, nothing is kept.
  template <typename T, typename Make>
  [[nodiscard]] std::shared_ptr<const T> get(const Make& make) const {
    const std::scoped_lock lock{mutex_};
    if (const std::shared_ptr<const void> held = held_.lock()) { return std::static_pointer_cast<const T>(held); }
    std::shared_ptr<const T> made = make();
    held_                         = made;
    return made;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::weak_ptr<const void> held_;
};

// A set's rows, from a cache line's start: where a row falls within a line - in one, or across two -
// then does not depend on what the allocator handed out before (internal/base/aligned_vector.hpp). Every
// table and index on the set reads them.
template <typename Tup>
using Rows = ::dreamor::internal::AlignedVector64<Tup>;

// What every copy of a set shares: its rows and the slot built from them.
template <typename Tup>
class SetRows {
 public:
  explicit SetRows(Rows<Tup> rows) : rows_{std::move(rows)} {}

 private:
  friend struct SetAccess;
  Rows<Tup> rows_;
  SharedSlot index_;
};

// Library-side access to a set's rows, so IndexSet itself never hands out its storage.
struct SetAccess {
  template <typename... Columns>
  [[nodiscard]] static const Rows<std::tuple<Columns...>>& rows(const IndexSet<Columns...>& set) noexcept {
    return set.rows_->rows_;
  }
  // The slot shared by every set with these rows - copies of one set, never an equal set.
  template <typename... Columns>
  [[nodiscard]] static const SharedSlot& indexSlot(const IndexSet<Columns...>& set) noexcept {
    return set.rows_->index_;
  }
  template <typename Tup>
  [[nodiscard]] static SetFor<Tup> make(Rows<Tup> rows) {
    return SetFor<Tup>{std::make_shared<const SetRows<Tup>>(std::move(rows))};
  }
};

}  // namespace internal::inline index_set

// A view in the ranges sense: copying it is O(1) and shares the rows, so views::all and
// sumOf hold it by value.
template <typename... Columns>
class IndexSet : public std::ranges::view_base {
  using Tup = std::tuple<Columns...>;

 public:
  using value_type     = Tup;
  using const_iterator = internal::Rows<Tup>::const_iterator;
  using iterator       = const_iterator;

  // Empty. Lets a model declare its sets as members and assign them once they are built.
  IndexSet() : rows_{emptyRows()} {}

  [[nodiscard]] const_iterator begin() const noexcept { return internal::SetAccess::rows(*this).begin(); }
  [[nodiscard]] const_iterator end() const noexcept { return internal::SetAccess::rows(*this).end(); }
  [[nodiscard]] std::size_t size() const noexcept { return internal::SetAccess::rows(*this).size(); }
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

  // --- Derivations (defined in set.hpp, algebra.hpp, grouped.hpp, ordered.hpp) ----------
  template <typename... Preds>
  [[nodiscard]] IndexSet where(Preds... preds) const;
  template <typename Key, typename Direction = Ascending>
  [[nodiscard]] IndexSet orderBy(Key key, Direction direction = {}) const;
  template <typename... Picked>
  [[nodiscard]] IndexSet<Picked...> select() const;
  template <typename Expr>
  [[nodiscard]] auto select(Expr expr) const;
  template <typename... By>
  [[nodiscard]] Grouped<IndexSet, By...> groupBy() const;
  [[nodiscard]] Membership<IndexSet> membership() const;
  template <typename... Within>
  [[nodiscard]] Ordered<IndexSet, Within...> ordered() const;

 private:
  friend struct internal::SetAccess;
  explicit IndexSet(std::shared_ptr<const internal::SetRows<Tup>> rows) : rows_{std::move(rows)} {}

  [[nodiscard]] static const std::shared_ptr<const internal::SetRows<Tup>>& emptyRows() {
    static const auto empty_rows = std::make_shared<const internal::SetRows<Tup>>(internal::Rows<Tup>{});
    return empty_rows;
  }

  std::shared_ptr<const internal::SetRows<Tup>> rows_;
};

}  // namespace dreamor
