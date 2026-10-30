// SPDX-License-Identifier: Apache-2.0
// File: value_table.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// Solver-free: plain doubles keyed like the table they came from, so the result of a solve
// outlives the model, the algorithm and the environment.
#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/index_set/call.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"
#include "dreamor/internal/row_index.hpp"

namespace dreamor {

template <typename... Keys>
class ValueTable;

namespace internal {
// A VarTable's bound and a ValueTable's value are given alike: a number, or a callable over the
// row's columns.
template <typename Bound, typename Row>
consteval void checkBound() {
  if constexpr (!std::is_arithmetic_v<Bound>) { checkColumnCallable<Bound, Row>(); }
}
template <typename Bound, typename Row>
[[nodiscard]] double boundOf(const Bound& bound, const Row& row) {
  if constexpr (std::is_arithmetic_v<Bound>) {
    return static_cast<double>(bound);
  } else {
    return static_cast<double>(callColumnsOf(bound, row));
  }
}

// The one way a backend's VarTable hands over its values.
struct ValueTableAccess {
  template <typename... Keys>
  [[nodiscard]] static ValueTable<Keys...> make(IndexSet<std::tuple<Keys...>> keys,
                                                std::shared_ptr<const RowIndex<std::tuple<Keys...>>> index,
                                                std::vector<double> values) {
    return ValueTable<Keys...>{std::move(keys), std::move(index), std::move(values)};
  }
};
}  // namespace internal

// A number per row of a set: the values of a VarTable's variables, read in one go by its
// solution() whatever the backend, or data on a set - AMPL's param {S} - built from the set and
// what each row's value is. Keyed like the set: yieldPlan(keys...) or yieldPlan(row), and
// range-for over (row, value).
//
//     const ValueTable<MachineCol, PeriodCol> hours{sRUN, available_hours};   // param hours {RUN}
template <typename... Keys>
class ValueTable {
 public:
  using Key = std::tuple<Keys...>;

  // Each row's value is `value`: a number, or a callable over the row's columns, as a VarTable's
  // bounds are. The set's row index is shared with every table built on it.
  template <typename Value>
  ValueTable(const IndexSet<Key>& keys, const Value& value) : keys_{keys}, index_{internal::sharedRowIndex(keys_)} {
    internal::checkBound<Value, Key>();
    values_.reserve(rows().size());
    for (const Key& row : rows()) {
      values_.push_back(internal::boundOf(value, row));
    }
  }
  // A set whose columns are not exactly the keys, in order, stops here on one sentence rather
  // than on "no matching constructor".
  template <typename OtherTup, typename Value>
    requires(!std::same_as<OtherTup, Key>)
  ValueTable(const IndexSet<OtherTup>& /*keys*/, const Value& /*value*/) {
    static_assert(internal::always_false<OtherTup>,
                  "A ValueTable is built on a set with exactly its key columns, in the same order: "
                  "ValueTable<MachineCol, PeriodCol> takes a SetOf<MachineCol, PeriodCol>. Declare the table in "
                  "the set's order, or derive a set in the table's order with select<...>().");
  }

  // The value of a key: yieldPlan(m, i, t) or yieldPlan(row), the columns in the declared
  // order (see index_set/key_args.hpp). Throws std::out_of_range when the set does not have
  // the key: a missing value read as 0 would pass silently for a variable at 0.
  template <typename... Args>
  [[nodiscard]] double operator()(const Args&... key) const {
    if constexpr (internal::checkTableKey<Key, Args...>()) {
      const std::size_t at = index_->find(rows(), key...);
      if (at == internal::RowIndex<Key>::kNotFound) {
        throw std::out_of_range("ValueTable: this tuple is not in the table's set.");
      }
      return values_[at];
    } else {
      return 0.0;
    }
  }
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... key) const {
    if constexpr (internal::checkTableKey<Key, Args...>()) {
      return index_->find(rows(), key...) != internal::RowIndex<Key>::kNotFound;
    } else {
      return false;
    }
  }
  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
  [[nodiscard]] const IndexSet<Key>& keys() const noexcept { return keys_; }
  [[nodiscard]] internal::RowPairIterator<Key, double> begin() const noexcept {
    return {rows().data(), values_.data()};
  }
  [[nodiscard]] internal::RowPairIterator<Key, double> end() const noexcept {
    return {rows().data() + rows().size(), values_.data() + values_.size()};
  }

 private:
  friend struct internal::ValueTableAccess;
  ValueTable(IndexSet<Key> keys, std::shared_ptr<const internal::RowIndex<Key>> index, std::vector<double> values)
      : keys_{std::move(keys)}, index_{std::move(index)}, values_{std::move(values)} {}

  [[nodiscard]] const std::vector<Key>& rows() const noexcept { return internal::SetAccess::rows(keys_); }

  IndexSet<Key> keys_;
  std::shared_ptr<const internal::RowIndex<Key>> index_;
  std::vector<double> values_;
};

}  // namespace dreamor

// A ValueTable names its key columns, so it is itself a column callable - a VarTable's bound, a
// value of another ValueTable - without a lambda that only looks it up: VarTable{env, sRUN, 0,
// hours, ...} bounds each Run[m,t] by hours[m,t]. Given a row of its own set, it finds the value
// by the row's address, as a VarTable finds a variable.
template <typename... Keys>
struct dreamor::PredicateColumns<dreamor::ValueTable<Keys...>> {
  using type = std::tuple<Keys...>;
};
template <typename... Keys>
struct dreamor::internal::TakesWholeRow<dreamor::ValueTable<Keys...>> : std::true_type {};
