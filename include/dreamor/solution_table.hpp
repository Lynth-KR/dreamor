// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// SolutionTable, which a solve returns: the values of a VarTable's variables, looked up by the table's keys.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/internal/backend/row_values.hpp"
#include "dreamor/internal/index_set/call.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/internal/index_set/name_token.hpp"

namespace dreamor::internal::inline backend {
struct SolutionTableAccess;  // Defined in internal/backend/var_table_core.hpp.
}  // namespace dreamor::internal::inline backend

namespace dreamor {

/// @section SolutionTable
///
/// The parts of this section hold the values of a solve. A `SolutionTable` holds one `double` for each variable of a
/// `VarTable`, and looks a value up by the key of the variable's row. The table holds copies of the values, so it can
/// be read after the model, the solver and the environment are gone. On every backend, only the `solution()` member
/// of a `VarTable` makes a `SolutionTable`.
///
/// A `SolutionTable` names its key columns, so it is a column callable. A `ParamTable` built from a `SolutionTable`
/// carries the values of a solve into the next model.
///
/// Guide: guide/solutions.md

/// The values of a `VarTable`'s variables after a solve: one `double` for each row of the table's set, in the order of
/// the set's rows.
template <typename... Keys>
class SolutionTable {
 public:
  using key_type = std::tuple<Keys...>;

  /// Returns the value of the row that `key` identifies.
  ///
  /// If the key's values can be written in a name, the message of the exception contains the key.
  ///
  /// Takes: `key` must be a key of the columns `Keys`, as the Keys section describes.
  /// Throws: `std::out_of_range` if the table's set has no row with the key.
  template <typename... Args>
  [[nodiscard]] double operator()(const Args&... key) const {
    // Throws for a key outside the set, because a missing value returned as 0 could not be told apart from a variable
    // whose value is 0.
    if constexpr (internal::checkTableKey<key_type, Args...>()) {
      const std::size_t at = find(key...);
      if (at == Values::kNotFound) {
        throw std::out_of_range("SolutionTable: no row " + internal::keyText(key...) + " in the table's set.");
      }
      return values_.at(at);
    } else {
      return 0.0;
    }
  }

  /// Checks whether the table's set has the row that `key` identifies.
  ///
  /// Takes: `key` must be a key of the columns `Keys`, as the Keys section describes.
  /// Returns: True if the set has the row; false otherwise.
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... key) const {
    if constexpr (internal::checkTableKey<key_type, Args...>()) {
      return find(key...) != Values::kNotFound;
    } else {
      return false;
    }
  }

  /// Returns the number of values, which equals the number of rows of the set.
  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

  /// Returns the set that the table is on, which is the set of the `VarTable` that made the table.
  [[nodiscard]] const IndexSet<Keys...>& keys() const noexcept { return values_.keys(); }

  /// Returns an iterator to the first pair of a row and its value.
  ///
  /// Iteration visits the rows in the set's order. Each element is a `std::pair` of a `const` reference to the row
  /// and the row's value.
  [[nodiscard]] auto begin() const noexcept { return values_.begin(); }

  /// Returns an iterator past the last pair of a row and its value.
  [[nodiscard]] auto end() const noexcept { return values_.end(); }

 private:
  using Values = internal::RowValues<double, Keys...>;

  // Returns the row of a key that checkTableKey accepts, or Values::kNotFound.
  template <typename... Args>
  [[nodiscard]] std::size_t find(const Args&... key) const {
    const auto find_columns = [this](const auto&... columns) { return values_.find(columns...); };
    return internal::findKey<key_type>(find_columns, Values::kNotFound, key...);
  }

  friend struct internal::SolutionTableAccess;

  explicit SolutionTable(Values values) : values_{std::move(values)} {}

  Values values_;
};

}  // namespace dreamor

// A SolutionTable lists its key columns, so it is a column callable with no lambda that only looks the value up. If a
// SolutionTable is given a row of its own set, it finds the value by the row's address, as a VarTable finds a
// variable.
template <typename... Keys>
struct dreamor::internal::CallableColumns<dreamor::SolutionTable<Keys...>> {
  using type = std::tuple<Keys...>;
};

template <typename... Keys>
struct dreamor::internal::TakesWholeRow<dreamor::SolutionTable<Keys...>> : std::true_type {};
