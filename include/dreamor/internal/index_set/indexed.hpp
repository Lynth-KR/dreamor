// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The class IndexSet and the storage that the copies of a set share. The members of IndexSet are defined in the
// headers that implement them: set.hpp, algebra.hpp, grouped.hpp and ordered.hpp.
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
#include "dreamor/internal/base/flat_index.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/key_args.hpp"

namespace dreamor {
template <typename... Columns>
class IndexSet;
template <typename Set, typename... Columns>
class Grouped;
template <typename Set, typename... Within>
class Ordered;
template <typename Set, bool In = true>
class Membership;
}  // namespace dreamor

namespace dreamor::internal::inline index_set {

// The set type whose rows are Tup: IndexSet<Cs...> for std::tuple<Cs...>. Code that works on rows computes a row
// type, and SetFor names the set type for it.
template <typename Tup>
struct SetForTuple;

template <typename... Columns>
struct SetForTuple<std::tuple<Columns...>> {
  using type = IndexSet<Columns...>;
};

template <typename Tup>
using SetFor = SetForTuple<Tup>::type;

// The column that IndexSet{records, row_of} deduces if the declaration does not name the columns. A deduction guide
// cannot read the columns from what row_of returns. So it deduces IndexSet<ColumnsNotDeduced>, and a static_assert
// in IndexSet tells the user to name the set's type.
struct ColumnsNotDeduced {};

// True if T is an IndexSet. A source that is a set can share its rows instead of copying them.
template <typename T>
inline constexpr bool is_index_set = false;
template <typename... Columns>
inline constexpr bool is_index_set<IndexSet<Columns...>> = true;

// Holds one object that is built from a set's rows and shared by every table on the set: the row index in which the
// variable and value tables look up keys (internal/backend/row_index.hpp). The slot holds the object through a
// weak_ptr. The object lives while a table that uses it lives, and the next table builds it again after that, so a
// set alone keeps no row index alive. The set layer does not know the object's type, so the slot stores it as a
// pointer to const void.
class SharedSlot {
 public:
  // Returns the held object, or calls make() if no object is held. make() runs under the lock, so a second table on
  // the same set waits for the index of the first table instead of building its own. If make() throws, the slot
  // keeps nothing.
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

// The storage of a set's rows, which begins at the start of a cache line. Whether a row lies in one cache line or
// across two then does not depend on the addresses that the allocator returned before
// (internal/base/aligned_vector.hpp). This matters because every table and index on the set reads these rows.
template <typename Tup>
using Rows = ::dreamor::internal::AlignedVector64<Tup>;

// The data that every copy of a set shares: the rows, the slot for the tables' row index, and the index in which
// contains() and Membership look up rows. That index is built once, on its first use, and is kept with the rows.
template <typename Tup>
class SetRows {
 public:
  explicit SetRows(Rows<Tup> rows) : rows_{std::move(rows)} {}

 private:
  friend struct SetAccess;
  Rows<Tup> rows_;
  SharedSlot index_;
  mutable std::once_flag lookup_built_;
  mutable std::unique_ptr<const ::dreamor::internal::FlatIndex<Tup>> lookup_;
};

// Gives the library access to a set's storage, so that IndexSet does not hand its storage out to users.
struct SetAccess {
  template <typename... Columns>
  [[nodiscard]] static const Rows<std::tuple<Columns...>>& rows(const IndexSet<Columns...>& set) noexcept {
    return set.rows_->rows_;
  }

  // Returns the slot that every copy of the set shares. An equal set that was made separately has a slot of its own.
  template <typename... Columns>
  [[nodiscard]] static const SharedSlot& indexSlot(const IndexSet<Columns...>& set) noexcept {
    return set.rows_->index_;
  }

  // Returns the index of the set's rows by value. The first call builds it with make, under a once_flag, so that
  // concurrent first calls build it only once.
  template <typename... Columns, typename Make>
  [[nodiscard]] static const ::dreamor::internal::FlatIndex<std::tuple<Columns...>>& lookup(
      const IndexSet<Columns...>& set, const Make& make) {
    const auto& shared = *set.rows_;
    std::call_once(shared.lookup_built_, [&] { shared.lookup_ = make(shared.rows_); });
    return *shared.lookup_;
  }

  template <typename Tup>
  [[nodiscard]] static SetFor<Tup> make(Rows<Tup> rows) {
    return SetFor<Tup>{std::make_shared<const SetRows<Tup>>(std::move(rows))};
  }

  // Returns a set on rows that are already shared. A ParamTable built from records uses it for the rows that
  // rowsOfRecords makes.
  template <typename Tup>
  [[nodiscard]] static SetFor<Tup> adopt(std::shared_ptr<const SetRows<Tup>> rows) {
    return SetFor<Tup>{std::move(rows)};
  }

  // Returns a handle to the empty rows of these columns, which every caller shares. The rows are a static object, and
  // the handle owns nothing, so making it allocates nothing and cannot throw: a set that is moved from takes it.
  template <typename Tup>
  [[nodiscard]] static std::shared_ptr<const SetRows<Tup>> emptyRows() noexcept {
    static const SetRows<Tup> rows{Rows<Tup>{}};
    return std::shared_ptr<const SetRows<Tup>>{std::shared_ptr<const void>{}, &rows};
  }

  // Returns the empty set of these columns. An operation returns it if it has nothing else to return, such as after a
  // static_assert has already reported an error.
  template <typename Tup>
  [[nodiscard]] static SetFor<Tup> empty() noexcept {
    return SetFor<Tup>{emptyRows<Tup>()};
  }
};

}  // namespace dreamor::internal::inline index_set

namespace dreamor {

/// @section IndexSet
///
/// An `IndexSet` holds the rows that a model's variables, constraints and sums run over, and its member functions
/// derive new sets from it. The rows of a set cannot be changed. A set is made in one of these ways:
///
/// - by `build()` of a chain, or of set-builder notation;
/// - by the constructor that makes a row from each record of a range;
/// - by `none()`, which returns a set with no rows;
/// - by a derivation from another set, such as `where`, `orderBy`, `select`, or an operation of the Set algebra
///   section.
///
/// A derivation returns a new set. The set that it reads does not change.
///
/// Guide: guide/deriving-sets.md

/// @hidden - the type of ascending
struct Ascending {};

/// @hidden - the type of descending
struct Descending {};

/// The direction of `orderBy` that puts the smallest key first.
///
/// `ascending` is the default direction of `orderBy`. Like `ASC` in SQL, `ascending` sorts from the smallest key to
/// the largest.
inline constexpr Ascending ascending{};  // NOLINT(readability-identifier-naming): a tag object, as std::in_place
/// The direction of `orderBy` that puts the largest key first.
///
/// Like `DESC` in SQL, `descending` sorts from the largest key to the smallest.
inline constexpr Descending descending{};  // NOLINT(readability-identifier-naming): a tag object, as std::in_place

/// A set of rows over the columns `Columns...`, with no row twice, in a fixed order.
///
/// Each row is a `std::tuple<Columns...>`. The rows cannot be changed after the set is made.
///
/// An `IndexSet` is a `std::ranges::view`. Copying a set takes constant time, and the copy shares the rows with the
/// original. The iterators of a set are random-access iterators over `const` rows.
///
/// A set that is constructed with no arguments is empty, as `none()` returns, and gets its rows when another set is
/// assigned to it: a set that is a member of a class can be made in the class's member initializer list, or assigned
/// in the constructor's body or by a function that builds the class's sets. An empty set is also built by a chain
/// over an empty source. A set that is moved from is empty, as a `std::vector` that is moved from is.
///
/// Takes: `Columns...` must be at most 64 columns. Each column must be declared by `DREAMOR_ENTITY_COLUMN` or
/// `DREAMOR_VALUE_COLUMN`.
template <typename... Columns>
class IndexSet : public std::ranges::view_base {
  // IndexSet is a view in the ranges sense, so that views::all and sumExpr hold a set by value.
  using Tup = std::tuple<Columns...>;

  static_assert(!(std::is_same_v<Columns, internal::ColumnsNotDeduced> || ...),
                "IndexSet{records, row_of} does not deduce its columns: name the set's type - IndexSet<cMachine, "
                "cItem> sROUTES{routes, route_of}, or a member's sROUTES_{routes_, route_of} in an initializer list.");
  static_assert(((internal::column<Columns> || std::is_same_v<Columns, internal::ColumnsNotDeduced>) && ...),
                "An IndexSet's type lists its columns, declared with DREAMOR_ENTITY_COLUMN or DREAMOR_VALUE_COLUMN: "
                "IndexSet<cMachine, cItem>, not IndexSet<Machine, Item> nor IndexSet<int>.");

 public:
  using value_type     = Tup;
  using const_iterator = internal::Rows<Tup>::const_iterator;
  using iterator       = const_iterator;

  /// @anchor indexset-constructor
  /// Constructs a set with one row for each element of `records`, in the order of `records`.
  ///
  /// For each record, the constructor calls `row_of` with the record, and `row_of` returns the record's row. The row
  /// is a `std::tuple`, or a single value for a set of one column. The type of each element of the row chooses the
  /// column of the set that the element fills, so the elements may come in any order:
  ///
  /// - A column of the set fills itself.
  /// - A column of another name, or a plain value, fills the one column of the set that holds its type.
  /// - If no column of the set holds its type, an integer fills the one column of integers, and a text fills the one
  ///   column of text.
  ///
  /// If two columns of the set hold the same type, such as `cFrom` and `cTo`, a plain value of that type could fill
  /// either column, so a plain value in their place does not compile.
  ///
  /// The rows hold copies of the values. A column of `std::string_view` is the exception: its values are views of
  /// the text inside the records, so the records must outlive the set.
  ///
  /// The type of the set must be written with its columns, because the constructor cannot deduce them from `row_of`.
  /// A declaration that names `IndexSet` without its columns does not compile.
  ///
  /// Takes: `records` must be an input range. A `const` `std::views::filter` is not an input range, because it
  /// cannot be iterated. `records` may be a temporary container, unless the set has a column of `std::string_view`.
  /// `row_of` must be callable with one element of `records`. It must return a value for each column of the set, and
  /// each value must fill a different column. If two columns hold the same type, `row_of` must return their values
  /// as those columns. If the set has a column of `std::string_view`, `records` must be an lvalue or a borrowed view
  /// that does not make its records as it is read, `row_of` must take each record by reference, and `row_of` must
  /// not return a `std::string` for that column.
  /// Contract: source_holds_no_repeat, source_holds_no_empty_ref
  template <typename Range, typename RowOf>
  IndexSet(Range&& records, const RowOf& row_of);  // Defined in set.hpp.

  /// @anchor indexset-constructor-empty
  /// Constructs a set with no rows, as `none()` returns.
  ///
  /// Cost: No memory is allocated.
  IndexSet() noexcept : rows_{internal::SetAccess::emptyRows<Tup>()} {}

  /// Returns a set with no rows.
  ///
  /// A set derived from the empty set is empty too. A variable family over the empty set holds no variables, and a
  /// sum over the empty set has no terms.
  ///
  /// Cost: No memory is allocated. Every empty set of the same columns shares one empty storage.
  [[nodiscard]] static IndexSet none() noexcept { return internal::SetAccess::empty<Tup>(); }

  /// Returns an iterator to the first row.
  ///
  /// Iteration visits the rows in the order of the set. The rows are `const`.
  [[nodiscard]] const_iterator begin() const noexcept { return internal::SetAccess::rows(*this).begin(); }

  /// Returns an iterator past the last row.
  [[nodiscard]] const_iterator end() const noexcept { return internal::SetAccess::rows(*this).end(); }

  /// Returns the number of rows.
  [[nodiscard]] std::size_t size() const noexcept { return internal::SetAccess::rows(*this).size(); }

  /// Checks whether the set has no rows.
  ///
  /// Returns: True if the set has no rows; false otherwise.
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

  /// Returns a new set with the rows for which every condition in `preds` is true, in the order of this set.
  ///
  /// Takes: `preds` must be one or more conditions. Each condition must be a column callable that reads only columns
  /// of the set.
  template <typename... Preds>
  [[nodiscard]] IndexSet where(Preds... preds) const;
  /// Returns a new set with the rows sorted by the values that `key` returns for them.
  ///
  /// `key` returns one value, or a `std::tuple` of values, which are compared from the first element to the last.
  /// `direction` is one of two values:
  ///
  /// - `ascending`, the default, puts the row with the smallest key first.
  /// - `descending` puts the row with the largest key first.
  ///
  /// The sort is stable: rows with equal keys keep their order in this set. A `Ref` compares by the primary key of
  /// its entity.
  ///
  /// Takes: `key` must be a column callable that reads only columns of the set. The values that `key` returns must
  /// satisfy `std::totally_ordered`. `direction` must be `ascending` or `descending`. If `direction` is left out, it
  /// is `ascending`.
  template <typename Key, typename Direction = Ascending>
  [[nodiscard]] IndexSet orderBy(Key key, Direction direction = {}) const;
  /// @anchor indexset-select-columns
  /// Returns the projection of the set onto the columns `Picked...`, in that order.
  ///
  /// Each row of this set gives a row with its values in the columns `Picked...`. If such a row repeats an earlier
  /// one, it is dropped. The new set holds each row once, in the order in which the rows first appear.
  ///
  /// Takes: `Picked...` must be one or more columns of the set. No column may be named twice.
  template <typename... Picked>
  [[nodiscard]] IndexSet<Picked...> select() const;
  /// @anchor indexset-select-expression
  /// Returns a new set whose rows are what `expr` returns for the rows of this set.
  ///
  /// `expr` returns a column, or a `std::tuple` of distinct columns. These columns may be columns that this set does
  /// not have, so `select(expr)` can rename a column or make a new column from the values of others. A value moves
  /// into another column only if `expr` names that column. If a row that `expr` returns repeats an earlier one, it
  /// is dropped. The new set holds each row once, in the order in which the rows first appear.
  ///
  /// Takes: `expr` must be a column callable that reads only columns of the set. `expr` must return a column or a
  /// `std::tuple` of one or more distinct columns. A plain value does not compile, because it does not say which
  /// column it fills.
  /// Returns: An `IndexSet` over the columns that `expr` returns, in the order in which `expr` returns them.
  template <typename Expr>
  [[nodiscard]] auto select(Expr expr) const;
  /// Returns the rows of the set, divided into groups by the values of the columns `By...`.
  ///
  /// The groups are in the order in which their keys first appear in the set. The rows of each group are in the
  /// order of the set. The `Grouped` shares the rows of the set and copies none of them.
  ///
  /// Takes: `By...` must be one or more columns of the set. No column may be named twice.
  /// Throws: `std::length_error` if the set has more than 2^32 - 2 rows.
  template <typename... By>
  [[nodiscard]] Grouped<IndexSet, By...> groupBy() const;
  /// Returns a condition that is true for the rows that are in this set.
  ///
  /// The `Membership` holds a copy of the set, which shares the rows. `!` applied to the `Membership` returns the
  /// opposite condition, which is true for the rows that are not in this set.
  ///
  /// Cost: If no copy of the set has built an index of its rows yet, `membership()` builds one. `contains` and every
  /// `Membership` of the set use the same index.
  [[nodiscard]] Membership<IndexSet> membership() const;
  /// Checks whether the set has the row that `key` identifies.
  ///
  /// Takes: `key` must be a key of the set, as the Keys section describes.
  /// Returns: True if the set has the row; false otherwise.
  /// Cost: The first call of `contains` or `membership()` on any copy of the set builds an index of the rows. The
  /// index is kept with the rows, and every copy of the set uses it.
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... key) const;

  /// @hidden - exists only to reject a key in braces with a message
  template <typename Unused = void>
  [[nodiscard]] bool contains(internal::BracedKey /*key*/) const {
    static_assert(internal::always_false<Unused>, DREAMOR_INTERNAL_NO_BRACES_MESSAGE);
    return false;
  }

  /// Returns the positions of the rows in the order of the whole set.
  ///
  /// The `Ordered` returns the position of a row, the first and the last row, and the rows before and after a row.
  /// Positions count from 0. The `Ordered` shares the rows of the set.
  [[nodiscard]] Ordered<IndexSet> ordered() const;
  /// Returns the positions of the rows within each group of rows that have the same values in the columns
  /// `Within...`.
  ///
  /// The groups are in the order in which their keys first appear in the set. The rows of each group are in the
  /// order of the set. The rows before and after a row are rows of its own group. The `Ordered` holds a copy of the
  /// rows, laid out group by group. To number the rows of the whole set, use `ordered()`.
  ///
  /// Takes: `Within...` must be one or more columns of the set. No column may be named twice.
  template <typename... Within>
  [[nodiscard]] Ordered<IndexSet, Within...> orderedWithin() const;
  /// @hidden - exists only to reject ordered<Columns...>() with a message that points to orderedWithin
  template <typename First, typename... Rest>
  [[nodiscard]] Ordered<IndexSet> ordered() const;

  /// @hidden - the copy and move operations; the class says what a moved-from set holds
  IndexSet(const IndexSet&) = default;

  /// @hidden - the copy and move operations; the class says what a moved-from set holds
  IndexSet(IndexSet&& other) noexcept : rows_{std::exchange(other.rows_, internal::SetAccess::emptyRows<Tup>())} {}

  /// @hidden - the copy and move operations; the class says what a moved-from set holds
  IndexSet& operator=(const IndexSet&) = default;

  /// @hidden - the copy and move operations; the class says what a moved-from set holds
  IndexSet& operator=(IndexSet&& other) noexcept {
    rows_ = std::exchange(other.rows_, internal::SetAccess::emptyRows<Tup>());
    return *this;
  }

  ~IndexSet() = default;

 private:
  friend struct internal::SetAccess;

  explicit IndexSet(std::shared_ptr<const internal::SetRows<Tup>> rows) : rows_{std::move(rows)} {}

  // Never null: a moved-from set holds the shared empty rows, so that every member stays valid on it.
  std::shared_ptr<const internal::SetRows<Tup>> rows_;
};

// If the declaration does not name the set's columns, IndexSet{records, row_of} deduces
// IndexSet<ColumnsNotDeduced>, and the first static_assert in IndexSet reports the error.
template <typename Range, typename RowOf>
IndexSet(Range&&, const RowOf&) -> IndexSet<internal::ColumnsNotDeduced>;

}  // namespace dreamor
