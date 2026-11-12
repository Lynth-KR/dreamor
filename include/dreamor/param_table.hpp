// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// ParamTable, a model's data on a set: one value for each row, looked up by the key of the row.
#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/backend/row_index.hpp"
#include "dreamor/internal/backend/row_values.hpp"
#include "dreamor/internal/index_set/binding.hpp"
#include "dreamor/internal/index_set/call.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/internal/index_set/name_token.hpp"
#include "dreamor/internal/index_set/set.hpp"

namespace dreamor::detail {

// Whether a ParamTable can hold values of type T: an object type that is copied in, one value for each row.
template <typename T>
inline constexpr bool kParamValue = std::is_object_v<T> && !std::is_array_v<T> && !std::is_const_v<T> &&
                                    !std::is_volatile_v<T> && std::is_copy_constructible_v<T>;

// Whether the value given to a ParamTable is one T for every row, not a callable over the row's columns. It is one T
// if it is a T, a number when T is a number, or a value of no class type that converts to T, such as a string
// literal for a std::string_view.
template <typename T, typename Value>
inline constexpr bool kOneValue =
    std::is_same_v<std::remove_cv_t<Value>, T> || (std::is_arithmetic_v<Value> && std::is_arithmetic_v<T>) ||
    (!std::is_class_v<Value> && std::is_convertible_v<const Value&, T>);

// Whether T views text that it does not hold, as a std::string_view does.
template <typename T>
inline constexpr bool kShowsText = std::is_same_v<T, std::string_view>;

// Whether the text that a callable returns outlives the call. A reference, a std::string_view and a pointer refer to
// text that the callable did not make for the call. A std::string returned by value is destroyed at the end of the
// full expression, and a view of it would then refer to freed memory.
template <typename Returned>
inline constexpr bool kKeepsText =
    std::is_reference_v<Returned> || std::is_same_v<std::remove_cv_t<Returned>, std::string_view> ||
    std::is_pointer_v<Returned>;

}  // namespace dreamor::detail

namespace dreamor {

/// @section ParamTable
///
/// The parts of this section hold a model's data on a set: one value for each row of the set. A `ParamTable` copies
/// the values in when the table is built, and looks a value up by the key of its row, as the set looks up a row.
///
/// A `ParamTable` names its key columns, so it is a column callable. A table of numbers can therefore be given where
/// a column callable that returns a number is taken: as a bound of a `VarTable`, as the term of a sum, or as the
/// values of another `ParamTable`. The values of a solve come back as a `SolutionTable`, which is a column callable
/// too. A `ParamTable` built from a `SolutionTable` carries the values of a solve into the next model.
///
/// Guide: guide/data-on-a-set.md

/// A value of type `T` for each row of a set, in the order of the set's rows.
///
/// The values are set when the table is constructed, and do not change after that. The table shares one index from
/// key to row with every other table on the same set, so the index is built once for the set.
///
/// `T` is typically a number, a `bool`, an enum, a text, a `Ref`, or a struct that holds several values of a row. If
/// `T` is `std::string`, the table holds a copy of each text. If `T` is `std::string_view`, the table holds a view of
/// each text, and the text must outlive the table.
///
/// Takes: `T` must be a type of object that can be copied. `T` must not be a pointer, a reference, an array, or a
/// `const` or `volatile` type. To refer to an entity, `T` must be a `Ref`, not a pointer.
template <typename T, typename... Keys>
class ParamTable {
  static_assert(!std::is_pointer_v<T>,
                "A ParamTable refers to an entity by a Ref, not a pointer: ParamTable<Ref<Machine>, cItem> holds "
                "the machine of each item.");
  static_assert(std::is_pointer_v<T> || detail::kParamValue<T>,
                "A ParamTable holds a value a row, copied in: a number, a bool, an enum, a text, a Ref, or a struct "
                "of the row's values - ParamTable<Lane, cFrom, cTo>, not a reference, an array or a const type.");

 public:
  using key_type   = std::tuple<Keys...>;
  using value_type = T;

  /// Constructs a table on the set `keys`, with the value of each row taken from `value`.
  ///
  /// `value` is one of these:
  ///
  /// - A value of `T`, which every row gets. A number of another arithmetic type is converted to `T` if `T` is a
  ///   number. A value that is not of a class type, such as a string literal, is converted to `T` if it converts.
  /// - A column callable that reads columns of `keys` and returns a value that converts to `T`. The callable is called
  ///   once for each row, in the set's order. Another `ParamTable` and a `SolutionTable` are such callables.
  ///
  /// A container of values does not compile, because the table cannot tell which value belongs to which row.
  ///
  /// Takes: `keys` must have exactly the columns `Keys`, in the same order. A set with other columns, or with the
  /// columns in another order, does not compile. If `T` is `std::string_view` and `value` is a callable, the callable
  /// must return a reference, a pointer or a `std::string_view` to text that outlives the table. A callable that
  /// returns a `std::string` by value does not compile.
  template <typename Value>
  ParamTable(const IndexSet<Keys...>& keys, const Value& value) : values_{make(keys, value)} {}

  /// @overload
  /// Constructs a table from `records`, with one row and one value for each record.
  ///
  /// The table's set is the set that the `IndexSet` constructor from records builds from `records` and `row_of`: one
  /// row for each record, in the order of `records`. For each record, `row_of` returns the record's row, under the
  /// rules of that constructor, and `value_of` returns the value of that row, which is converted to `T`. The
  /// constructor reads `records` twice: once for the rows and once for the values.
  ///
  /// The table holds a copy of each value. If `T` is `std::string_view`, each value is a view of text inside a record,
  /// so the records must outlive the table.
  ///
  /// Takes: `records` must be a forward range, such as a container or a view of one, because it is read twice.
  /// `records` may be a temporary container, unless `T` or a column of `Keys` holds `std::string_view`. `row_of` must
  /// be callable with one record, and must return the record's row as the `IndexSet` constructor from records takes
  /// it. `value_of` must be callable with one record, and must return a value that converts to `T`. If `T` is
  /// `std::string_view`, `records` must be an lvalue container or a borrowed view of one, `value_of` must take each
  /// record by reference, and `value_of` must return a reference, a pointer or a `std::string_view` to the record's own
  /// text.
  /// Contract: source_holds_no_repeat, source_holds_no_empty_ref
  template <typename Range, typename RowOf, typename ValueOf>
  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): the records are only read, one by one
  ParamTable(Range&& records, const RowOf& row_of, const ValueOf& value_of)
      : values_{fromRecords<internal::keepsElements<Range>()>(records, row_of, value_of)} {}

  /// @hidden - exists only to reject, with a message, a set whose columns are not the table's key columns
  template <typename Set, typename Value>
    requires(internal::is_index_set<Set> && !std::same_as<Set, IndexSet<Keys...>>)
  ParamTable(const Set& /*keys*/, const Value& /*value*/)
      : values_{internal::SetAccess::empty<key_type>(), nullptr, {}} {
    static_assert(internal::always_false<Set>,
                  "A ParamTable is built on a set with exactly its key columns, in the same order: "
                  "ParamTable<double, cMachine, cPeriod> takes an IndexSet<cMachine, cPeriod>. Declare the table "
                  "in the set's order, or derive a set in the table's order with select<...>().");
  }

  /// Returns the value of the row that `key` identifies.
  ///
  /// If the key's values can be written in a name, the message of the exception contains the key.
  ///
  /// Takes: `key` must be a key of the columns `Keys`, as the Keys section describes.
  /// Returns: A `const` reference to the value. If `T` is `bool`, the value itself.
  /// Throws: `std::out_of_range` if the table's set has no row with the key.
  template <typename... Args>
  [[nodiscard]] decltype(auto) operator()(const Args&... key) const {
    if constexpr (internal::checkTableKey<key_type, Args...>()) {
      const std::size_t at = find(key...);
      if (at == Values::kNotFound) {
        throw std::out_of_range("ParamTable: no row " + internal::keyText(key...) + " in the table's set.");
      }
      return values_.at(at);
    } else {
      return T{};
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

  /// Returns the set that the table is on.
  [[nodiscard]] const IndexSet<Keys...>& keys() const noexcept { return values_.keys(); }

  /// Returns an iterator to the first pair of a row and its value.
  ///
  /// Iteration visits the rows in the set's order. Each element is a `std::pair` of a `const` reference to the row
  /// and a copy of the row's value.
  [[nodiscard]] auto begin() const noexcept { return values_.begin(); }

  /// Returns an iterator past the last pair of a row and its value.
  [[nodiscard]] auto end() const noexcept { return values_.end(); }

 private:
  using Values = internal::RowValues<T, Keys...>;

  // Returns the row of a key that checkTableKey accepts, or Values::kNotFound.
  template <typename... Args>
  [[nodiscard]] std::size_t find(const Args&... key) const {
    const auto find_columns = [this](const auto&... columns) { return values_.find(columns...); };
    return internal::findKey<key_type>(find_columns, Values::kNotFound, key...);
  }

  template <bool KeepsRecords, typename Range, typename RowOf, typename ValueOf>
  [[nodiscard]] static Values fromRecords(Range& records, const RowOf& row_of, const ValueOf& value_of) {
    // The records are read twice, first for the rows and then for the values. A range that can be read only once
    // would yield no values on the second read.
    static_assert(std::ranges::forward_range<Range> || !std::ranges::input_range<Range>,
                  "ParamTable{records, row_of, value_of} reads the records twice, for the rows and for the values: "
                  "hand it a range that can be read again - a container, or a view of one.");
    const auto keys = internal::SetAccess::adopt(internal::rowsOfRecords<key_type, KeepsRecords>(records, row_of));
    std::vector<internal::StoredValue<T>> values;
    if constexpr (std::ranges::input_range<Range>) {
      using Record             = std::ranges::range_reference_t<Range>;
      constexpr bool kCallable = std::invocable<const ValueOf&, Record>;
      static_assert(kCallable,
                    "ParamTable{records, row_of, value_of}: value_of takes one record and returns its value - a "
                    "callable over the record's type, or `const auto&`.");
      if constexpr (kCallable) {
        using Returned = std::invoke_result_t<const ValueOf&, Record>;
        static_assert(std::is_convertible_v<Returned, T>,
                      "ParamTable{records, row_of, value_of}: value_of returns something else than the table's T.");
        static_assert(!detail::kShowsText<T> ||
                          (KeepsRecords && detail::kKeepsText<Returned> && !internal::takesRecordCopy<ValueOf>()),
                      "ParamTable{records, row_of, value_of} of texts shows each text where value_of found it: hand "
                      "it a named container of records, take each by reference - [](const Record& r) - and "
                      "return a reference or a std::string_view to the record's own text.");
        if constexpr (std::is_convertible_v<Returned, T>) {
          values.reserve(internal::SetAccess::rows(keys).size());
          for (auto&& record : records) {
            values.push_back(static_cast<internal::StoredValue<T>>(static_cast<T>(std::invoke(value_of, record))));
          }
        }
      }
    }
    return Values{keys, internal::sharedRowIndex(keys), std::move(values)};
  }

  template <typename Value>
  [[nodiscard]] static Values make(const IndexSet<Keys...>& keys, const Value& value) {
    std::vector<internal::StoredValue<T>> values;
    if constexpr (detail::kOneValue<T, Value>) {
      values.assign(internal::SetAccess::rows(keys).size(),
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): a string literal for a text
                    static_cast<internal::StoredValue<T>>(static_cast<T>(value)));
    } else if constexpr (internal::kNotCallable<Value>) {
      // A value that converts to T, such as a std::string for a table of std::string_view, is told so first: a
      // std::string is a range as well, and the message about a container would mislead.
      constexpr bool kConverts = std::is_convertible_v<const Value&, T>;
      static_assert(!kConverts,
                    "One value for every row is a T itself, not a value that converts to one: write the T - "
                    "std::string_view{label} for a ParamTable of std::string_view, with label outliving the table.");
      static_assert(kConverts || !std::ranges::range<const Value&>,
                    "A ParamTable takes a value for each row from a callable over the row's columns - "
                    "`[&capacity](cMachine m) { return capacity.at(m->id); }` - not from a container, whose order "
                    "no row knows.");
      static_assert(kConverts || std::ranges::range<const Value&>,
                    "This value is not a T, nor a callable over the row's columns: one value for every row is a "
                    "T - one of its type, not another - and a value per "
                    "row comes from a callable, `[](cMachine m) { return m->capacity; }`.");
    } else {
      internal::checkColumnCallable<Value, key_type>();
      if constexpr (internal::ColumnsOfPredicate<Value>::known &&
                    internal::ColumnsOfPredicate<Value>::no_mutable_reference) {
        using Returned = decltype(internal::callColumnsOf(value, std::declval<const key_type&>()));
        static_assert(std::is_convertible_v<Returned, T>,
                      "A ParamTable's value is one T for every row, or a callable over the row's columns that "
                      "returns a T: this one returns something else.");
        static_assert(!detail::kShowsText<T> || detail::kKeepsText<Returned>,
                      "A ParamTable of texts shows each text where its callable found it, and this callable "
                      "returns a std::string by value, gone once the call ends: return a reference to one that "
                      "outlives the table - `-> const std::string&` - or a std::string_view of it.");
        if constexpr (std::is_convertible_v<Returned, T>) {
          values.reserve(internal::SetAccess::rows(keys).size());
          for (const key_type& row : internal::SetAccess::rows(keys)) {
            values.push_back(
                static_cast<internal::StoredValue<T>>(static_cast<T>(internal::callColumnsOf(value, row))));
          }
        }
      }
    }
    return Values{keys, internal::sharedRowIndex(keys), std::move(values)};
  }

  Values values_;
};

}  // namespace dreamor

// A ParamTable lists its key columns, so it is a column callable with no lambda that only looks the value up. If a
// ParamTable is given a row of its own set, it finds the value by the row's address, as a VarTable finds a variable.
template <typename T, typename... Keys>
struct dreamor::internal::CallableColumns<dreamor::ParamTable<T, Keys...>> {
  using type = std::tuple<Keys...>;
};

template <typename T, typename... Keys>
struct dreamor::internal::TakesWholeRow<dreamor::ParamTable<T, Keys...>> : std::true_type {};
