// SPDX-License-Identifier: Apache-2.0
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
//     S.orderBy(key)             stable reorder by key
//     S.select<MachineCol, ...>()   projection (or column reorder), as a set
//     S.select(expr)             each row as expr makes it, as a set   setof {(i,j) in S} (j,i)
//     S.groupBy<OrderCol>()       Grouped: walk groups, slice by key
//     S.membership()             O(1) membership predicate over S's columns
//     S.ordered()                ord / next / prev
//     S.ordered<MachineCol>()    the same within each machine's rows   next(t, S[m])
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

namespace dreamor {

template <typename Tup>
class IndexSet;
template <typename Tup, typename... Columns>
class Grouped;
template <typename Tup, typename... Within>
class Ordered;
template <typename Tup>
class Membership;

// SetOf<MachineCol, ItemCol> reads as the set it declares.
template <typename... Columns>
using SetOf = IndexSet<std::tuple<Columns...>>;

namespace internal {

// One object built from a set's rows and shared by everything built on them - the row index
// every variable and value table on the set looks keys up in (internal/row_index.hpp). Held
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

// What every copy of a set shares: its rows and the slot built from them.
template <typename Tup>
class SetRows {
 public:
  explicit SetRows(std::vector<Tup> rows) : rows_{std::move(rows)} {}

 private:
  friend struct SetAccess;
  std::vector<Tup> rows_;
  SharedSlot index_;
};

// Library-side access to a set's rows, so IndexSet itself never hands out its storage.
struct SetAccess {
  template <typename Tup>
  [[nodiscard]] static const std::vector<Tup>& rows(const IndexSet<Tup>& set) noexcept {
    return set.rows_->rows_;
  }
  // The slot shared by every set with these rows - copies of one set, never an equal set.
  template <typename Tup>
  [[nodiscard]] static const SharedSlot& indexSlot(const IndexSet<Tup>& set) noexcept {
    return set.rows_->index_;
  }
  template <typename Tup>
  [[nodiscard]] static IndexSet<Tup> make(std::vector<Tup> rows) {
    return IndexSet<Tup>{std::make_shared<const SetRows<Tup>>(std::move(rows))};
  }
};

}  // namespace internal

// A view in the ranges sense: copying it is O(1) and shares the rows, so views::all and
// sumOf hold it by value.
template <typename Tup>
class IndexSet : public std::ranges::view_base {
 public:
  using value_type     = Tup;
  using const_iterator = std::vector<Tup>::const_iterator;
  using iterator       = const_iterator;

  // Empty. Lets a model declare its sets as members and assign them once they are built.
  IndexSet() : rows_{empty()} {}

  [[nodiscard]] const_iterator begin() const noexcept { return internal::SetAccess::rows(*this).begin(); }
  [[nodiscard]] const_iterator end() const noexcept { return internal::SetAccess::rows(*this).end(); }
  [[nodiscard]] std::size_t size() const noexcept { return internal::SetAccess::rows(*this).size(); }

  // --- Derivations (defined in set.hpp, algebra.hpp, grouped.hpp, ordered.hpp) ----------
  template <typename... Preds>
  [[nodiscard]] IndexSet where(Preds... preds) const;
  template <typename Key>
  [[nodiscard]] IndexSet orderBy(Key key) const;
  template <typename... Columns>
  [[nodiscard]] IndexSet<std::tuple<Columns...>> select() const;
  template <typename Expr>
  [[nodiscard]] auto select(Expr expr) const;
  template <typename... Columns>
  [[nodiscard]] Grouped<Tup, Columns...> groupBy() const;
  [[nodiscard]] Membership<Tup> membership() const;
  template <typename... Within>
  [[nodiscard]] Ordered<Tup, Within...> ordered() const;

 private:
  friend struct internal::SetAccess;
  explicit IndexSet(std::shared_ptr<const internal::SetRows<Tup>> rows) : rows_{std::move(rows)} {}

  [[nodiscard]] static const std::shared_ptr<const internal::SetRows<Tup>>& empty() {
    static const auto empty_rows = std::make_shared<const internal::SetRows<Tup>>(std::vector<Tup>{});
    return empty_rows;
  }

  std::shared_ptr<const internal::SetRows<Tup>> rows_;
};

}  // namespace dreamor
