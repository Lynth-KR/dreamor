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
//     S.distinct()               first occurrence of each row
//     S.select<MachineCol, ...>()   projection (or column reorder), as a set
//     S.groupBy<OrderCol>()       Grouped: walk groups, slice by key
//     S.membership()             O(1) membership predicate over S's columns
//     S.ordered()                ord / next / prev
//     S.assertUnique<...>()      debug check; returns S itself
//     unite(A, B), intersect(A, B), difference(A, B), concat(A, B)
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
template <typename Tup>
class Ordered;
template <typename Tup>
class Membership;

// SetOf<MachineCol, ItemCol> reads as the set it declares.
template <typename... Columns>
using SetOf = IndexSet<std::tuple<Columns...>>;

namespace internal {

// Library-side access to a set's rows, so IndexSet itself never hands out its storage.
struct SetAccess {
  template <typename Tup>
  [[nodiscard]] static const std::vector<Tup>& rows(const IndexSet<Tup>& set) noexcept {
    return *set.rows_;
  }
  template <typename Tup>
  [[nodiscard]] static IndexSet<Tup> make(std::vector<Tup> rows) {
    return IndexSet<Tup>{std::make_shared<const std::vector<Tup>>(std::move(rows))};
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

  [[nodiscard]] const_iterator begin() const noexcept { return rows_->begin(); }
  [[nodiscard]] const_iterator end() const noexcept { return rows_->end(); }
  [[nodiscard]] std::size_t size() const noexcept { return rows_->size(); }

  // --- Derivations (defined in set.hpp, algebra.hpp, grouped.hpp, ordered.hpp) ----------
  template <typename... Preds>
  [[nodiscard]] IndexSet where(Preds... preds) const;
  template <typename Key>
  [[nodiscard]] IndexSet orderBy(Key key) const;
  [[nodiscard]] IndexSet distinct() const;
  template <typename... Columns>
  [[nodiscard]] IndexSet<std::tuple<Columns...>> select() const;
  template <typename... Columns>
  [[nodiscard]] Grouped<Tup, Columns...> groupBy() const;
  [[nodiscard]] Membership<Tup> membership() const;
  [[nodiscard]] Ordered<Tup> ordered() const;
  // Not [[nodiscard]]: both `const auto S = ....assertUnique();` and a bare
  // `S.assertUnique<MachineCol, ItemCol>();` check are intended uses.
  template <typename... Columns>
  IndexSet assertUnique() const;  // NOLINT(modernize-use-nodiscard): see above

 private:
  friend struct internal::SetAccess;
  explicit IndexSet(std::shared_ptr<const std::vector<Tup>> rows) : rows_{std::move(rows)} {}

  [[nodiscard]] static const std::shared_ptr<const std::vector<Tup>>& empty() {
    static const auto empty_rows = std::make_shared<const std::vector<Tup>>();
    return empty_rows;
  }

  std::shared_ptr<const std::vector<Tup>> rows_;
};

}  // namespace dreamor
