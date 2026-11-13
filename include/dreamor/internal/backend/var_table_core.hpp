// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// VarTableCore, the part of every backend's VarTable that does not depend on the backend: the set, the index from key
// to row, the variables in the order of the rows, lookup and iteration. A backend's VarTable makes the variables into
// vars_ and adds solution().
#pragma once

#include <concepts>
#include <cstddef>
#include <exception>
#include <format>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/format_buffer.hpp"
#include "dreamor/internal/backend/row_index.hpp"
#include "dreamor/internal/index_set/call.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/internal/index_set/name_token.hpp"
#include "dreamor/logger.hpp"
#include "dreamor/solution_table.hpp"

// The message for a VarTable in a multiplication, shared by the two operators that reject it.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_SCALE_THE_SUM_MESSAGE                                                                    \
  "Scale the sum, not the table: 100 * sumExpr(env, rows, vYield).build() (sumExpr(rows, vYield) on MathOpt and " \
  "HiGHS), or multiply inside a term. A VarTable is a family of variables, not an expression."

// The message for nullptr given as a name. A name is a std::string_view, and a std::string_view made from a null
// pointer is undefined behavior.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_NULL_NAME_MESSAGE \
  "A name is text, and nullptr is not one: leave the name out, or pass {}, for none."

namespace dreamor::internal::inline backend {
// The only way for a backend's VarTable to construct a SolutionTable, whose constructor is private.
struct SolutionTableAccess {
  template <typename... Keys>
  [[nodiscard]] static SolutionTable<Keys...> make(IndexSet<Keys...> keys,
                                                   std::shared_ptr<const RowIndex<std::tuple<Keys...>>> index,
                                                   std::vector<double> values) {
    return SolutionTable<Keys...>{RowValues<double, Keys...>{std::move(keys), std::move(index), std::move(values)}};
  }
};

// The empty base of every backend's VarTable, through VarTableCore. The traits at the end of this file recognize a
// table by this base.
struct VarTableBase {};

// How a backend stores its variables, one for each row of the set. By default, the storage is a vector of the
// solver's handles. A backend that computes a row's variable from the row's position specializes VarStorage: on HiGHS,
// a row's column is the table's first column plus the row's position. A specialization provides size() and
// operator[], and an overload of cursorAt(storage, position) for iteration.
template <typename Var>
struct VarStorage {
  using type = std::vector<Var>;
};

template <typename Var>
[[nodiscard]] const Var* cursorAt(const std::vector<Var>& vars, std::size_t at) noexcept {
  return vars.data() + at;
}

/// @section VarTable
///
/// The parts of this section look up and iterate a family of variables: one variable for each row of a set. A
/// backend's `VarTable` constructor makes all of the variables at once. After that, the table's set and its variables
/// do not change. A table that is constructed with no arguments, or moved from, is empty: it has no rows and no
/// variables, as an empty set has none. Such a table gets its family when a table is moved into it, once: a table that
/// is a member of a class can be made in the class's member initializer list, or assigned in the constructor's body or
/// by a function that builds the class's families. Every backend's `VarTable` derives from `VarTableCore` and has the
/// members of this section. The constructors and `solution()` of each backend's `VarTable` are in that backend's
/// section.
///
/// Each variable is named `<name>_<key0>_<key1>...`: the name given to the constructor, then the value of each
/// column of the row, each one after `_`. The Names section says how each value is written. If the name given to the
/// constructor is empty, the variables have no names. A name longer than 255 characters is cut to its first 255
/// characters, and a warning is written to the log.
///
/// A table names its key columns, so it is a column callable. A table can be the term of a sum: the sum adds up the
/// variable of each row. The rows of the sum must have every key column of the table, and may have other columns
/// too: the table reads only its key columns from each row.
///
/// A table multiplied by a number does not compile, because a table is not an expression. To scale the variables,
/// multiply the built sum, or multiply inside the term.
///
/// Guide: guide/variable-families.md

/// @exposed
/// The base class of every backend's `VarTable`, which holds the set, an index from key to row, and the variables in
/// the order of the set's rows.
///
/// A model calls the members of this class through a backend's `VarTable`, and does not name the class.
///
/// A table cannot be copied, because a copy would refer to the same variables as a second family. A table can be
/// moved, and a family can be moved into a table that holds no variables, once (table_filled_once): moved over a
/// family, it would leave the rows that use the old variables on them. Destroying a table does not remove its
/// variables from the model.
///
/// Takes: `Keys` must be one or more distinct columns. Each column of `Keys` must hold a value that can be written in
/// a name, as the Names section describes.
template <typename Var, typename... Keys>
  requires(sizeof...(Keys) > 0 && (nameable<Keys> && ...))
class VarTableCore : public VarTableBase {
  static_assert(allColumnsDistinct<std::tuple<Keys...>>(std::index_sequence_for<Keys...>{}),
                "VarTable<cI, cI> names one key column twice, and no set has the same column twice: give each "
                "key its own column type.");

 public:
  // The row type of the set. A row of the set, of a subset of it, or of one of its groups is therefore one argument
  // that operator() accepts as it is.
  using key_type = std::tuple<Keys...>;

  /// Checks whether the table's set has the row that `key` identifies.
  ///
  /// Takes: `key` must be a key of the columns `Keys`, as the Keys section describes.
  /// Returns: True if the set has the row; false otherwise.
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... key) const {
    if constexpr (checkTableKey<key_type, Args...>()) {
      return findRow(key...) != RowIndex<key_type>::kNotFound;
    } else {
      return false;
    }
  }

  /// @hidden - exists only to reject, with a message, a set whose columns are not the table's key columns
  template <typename Model, typename Set, typename... Rest>
    requires(is_index_set<Set> && !std::same_as<Set, IndexSet<Keys...>>)
  VarTableCore(Model&& /*model*/, const Set& /*set*/, Rest&&... /*rest*/) : VarTableCore{SetAccess::empty<key_type>()} {
    // This constructor is public because a backend inherits it with `using Core::Core;`, which keeps its access. Its
    // first parameter is the backend's environment or model.
    static_assert(always_false<Set>,
                  "A VarTable is built on a set with exactly its key columns, in the same order: "
                  "VarTable<cMachine, cItem, cPeriod> takes an IndexSet<cMachine, cItem, cPeriod>. "
                  "Declare the table in the set's order, or derive a set in the table's order with select<...>().");
  }

  /// Returns the variable of the row that `key` identifies.
  ///
  /// If the key's values can be written in a name, the message of the exception contains the key.
  ///
  /// Takes: `key` must be a key of the columns `Keys`, as the Keys section describes.
  /// Throws: `std::out_of_range` if the table's set has no row with the key.
  template <typename... Args>
  [[nodiscard]] Var operator()(const Args&... key) const {
    if constexpr (checkTableKey<key_type, Args...>()) {
      return vars_[rowOf(key...)];
    } else {
      std::terminate();
    }
  }

  /// Returns the number of variables, which equals the number of rows of the set.
  [[nodiscard]] std::size_t size() const noexcept { return vars_.size(); }

  /// Returns the set that the table was built on.
  [[nodiscard]] const IndexSet<Keys...>& keys() const noexcept { return keys_; }

  /// Returns an iterator to the first pair of a row and its variable.
  ///
  /// Iteration visits the rows in the set's order. The backend's constructor made the variables in the same order.
  /// Each element is a `std::pair` of a `const` reference to the row and the row's variable.
  [[nodiscard]] auto begin() const noexcept { return Iterator{rows().data(), cursorAt(vars_, 0)}; }

  /// Returns an iterator past the last pair of a row and its variable.
  [[nodiscard]] auto end() const noexcept { return Iterator{rows().data() + rows().size(), cursorAt(vars_, size())}; }

  VarTableCore(const VarTableCore&)            = delete;
  VarTableCore& operator=(const VarTableCore&) = delete;

  /// @hidden - exists only to reject, with a message, a table multiplied by a number
  template <typename Scale, typename Table>
    requires std::is_base_of_v<VarTableCore, Table>
  friend auto operator*(const Scale& /*scale*/, const Table& /*table*/) {
    // These operators are hidden friends, so they are candidates only if a table is an operand. Each one takes the
    // backend's own table type, so that its match is exact. The return type is deduced, so that the static_assert is
    // instantiated as soon as overload resolution chooses the operator.
    static_assert(always_false<Scale>, DREAMOR_INTERNAL_SCALE_THE_SUM_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, a table multiplied by a number
  template <typename Table, typename Scale>
    requires(std::is_base_of_v<VarTableCore, Table> && !std::is_base_of_v<VarTableCore, Scale>)
  friend auto operator*(const Table& /*table*/, const Scale& /*scale*/) {
    static_assert(always_false<Scale>, DREAMOR_INTERNAL_SCALE_THE_SUM_MESSAGE);
    return false;
  }

 protected:
  // The empty set, the empty index and no variable: what a backend's default constructor makes, and a move leaves.
  VarTableCore() noexcept : index_{emptyRowIndex<key_type>()} {}

  explicit VarTableCore(const IndexSet<Keys...>& keys) : keys_{keys}, index_{sharedRowIndex(keys_)} {}

  // A move leaves the empty set, the empty index and no variable. A HiGHS table's variables are a first column and a
  // count, which a defaulted move would copy, so they are reset too.
  VarTableCore(VarTableCore&& other) noexcept
      : vars_{std::exchange(other.vars_, {})},
        keys_{std::move(other.keys_)},
        index_{std::exchange(other.index_, emptyRowIndex<key_type>())} {}

 public:
  /// Moves the family of `other` into this table, which holds no variables, and leaves `other` empty.
  ///
  /// Returns: This table.
  /// Contract: table_filled_once
  VarTableCore& operator=(VarTableCore&& other) noexcept {
    if (this != &other) {
      if (size() != 0) {
        DREAMOR_DEBUG_FATAL(
            "VarTable is assigned over a family of {} variables - a broken contract: a table is filled once. Move a "
            "family into a table that holds none, one constructed with no arguments, or declare another table.",
            size());
      }
      vars_  = std::exchange(other.vars_, {});
      keys_  = std::move(other.keys_);
      index_ = std::exchange(other.index_, emptyRowIndex<key_type>());
    }
    return *this;
  }

  /// @contract table_filled_once: When a table is assigned to, it holds no variables: it was constructed with no
  /// arguments, moved from, or made on an empty set. A family assigned over another would leave the rows and the
  /// objective that use the old variables on them, while the table's lookups return the new ones.

 protected:
  // Protected, so that no table is destroyed through a pointer to this base. The destructor ends no solver object,
  // because each object belongs to its environment or model.
  ~VarTableCore() noexcept = default;

  // Returns the row of a key passed to operator(), or throws std::out_of_range. Every backend throws for a key outside
  // the set, because such a key is a modeling error: a null handle or a 0 in its place would show the error only
  // later. The callers check the arguments with checkTableKey first.
  template <typename... Args>
  [[nodiscard]] std::size_t rowOf(const Args&... key) const {
    const std::size_t at = findRow(key...);
    if (at == RowIndex<key_type>::kNotFound) {
      throw std::out_of_range("VarTable: no row " + keyText(key...) + " in the table's set.");
    }
    return at;
  }

  [[nodiscard]] const Rows<key_type>& rows() const noexcept { return SetAccess::rows(keys_); }

  // Returns the row of a key that checkTableKey accepts, or kNotFound.
  template <typename... Args>
  [[nodiscard]] std::size_t findRow(const Args&... key) const {
    const auto find = [this](const auto&... columns) { return index_->find(rows(), columns...); };
    return findKey<key_type>(find, RowIndex<key_type>::kNotFound, key...);
  }

  // Returns the values of vars_, in the same order, as a SolutionTable that shares this table's set and index.
  [[nodiscard]] SolutionTable<Keys...> solutionTable(std::vector<double> values) const {
    return SolutionTableAccess::make<Keys...>(keys_, index_, std::move(values));
  }

  // One buffer serves the whole construction. The family's name stays at the start of the buffer, and only the part
  // after it is rewritten for each key, so naming the variables allocates no memory.
  //
  // Under kLogAndClear, a name too long for the buffer is cut to kNameBufferSize - 1 characters, a warning is written
  // to the log, and the table is built anyway.
  static constexpr std::size_t kNameBufferSize = 256;  // The longest name, with its terminating null character.
  using NameBuilder = ::dreamor::NameBuilder<kNameBufferSize, ::dreamor::OnFormatOverflow::kLogAndClear>;

  // Returns the builder of a family's names, with the family's name as the root. If the family's name is empty, the
  // builder is disabled, and the table names no variable.
  [[nodiscard]] static NameBuilder namesFor(std::string_view family) {
    NameBuilder names{nullptr};
    if (!family.empty()) { static_cast<void>(names.restart(family)); }
    return names;
  }

  [[nodiscard]] static const char* nameOf(NameBuilder& names, const key_type& key) {
    if (!names.enabled()) {
      return nullptr;  // The variables have no names, so no key is read or formatted.
    }
    std::size_t length = names.rootLength();
    std::apply([&](const auto&... columns) { ((length = appendName(names, length, columns, "_")), ...); }, key);
    return names.name();
  }

  // NOLINTNEXTLINE(*-non-private-member-variables-in-classes): each backend's VarTable fills it
  VarStorage<Var>::type vars_;  // vars_[i] is the variable of row i.

 private:
  template <typename>
  friend struct internal::TakesRowNumber;  // A sum over a group of the set reads vars_ by the rows' positions.
  using Iterator =
      RowPairIterator<key_type, decltype(cursorAt(std::declval<const typename VarStorage<Var>::type&>(), 0))>;

  IndexSet<Keys...> keys_;                           // Shares the rows of the set given to the constructor.
  std::shared_ptr<const RowIndex<key_type>> index_;  // Shared with every other table on the same set.
};

}  // namespace dreamor::internal::inline backend

// A table lists its key columns, so it is a column callable, and sumExpr takes a table as a term with no lambda that
// only looks the variable up. The specialization covers every backend's table through VarTableBase.
template <typename Table>
  requires std::derived_from<Table, dreamor::internal::VarTableBase>
struct dreamor::internal::CallableColumns<Table> {
  using type = Table::key_type;
};

// If a table is given a row of its own set, it finds the variable by the row's address.
template <typename Table>
  requires std::derived_from<Table, dreamor::internal::VarTableBase>
struct dreamor::internal::TakesWholeRow<Table> : std::true_type {};

// The variable of the row at position n of the set is vars_[n].
template <typename Table>
  requires std::derived_from<Table, dreamor::internal::VarTableBase>
struct dreamor::internal::TakesRowNumber<Table> : std::true_type {
  [[nodiscard]] static const Table::key_type* firstRow(const Table& table) noexcept {
    return SetAccess::rows(table.keys()).data();
  }

  [[nodiscard]] static auto at(const Table& table, std::size_t n) { return table.vars_[n]; }
};
