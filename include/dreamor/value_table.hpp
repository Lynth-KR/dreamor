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

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"
#include "dreamor/internal/row_index.hpp"

namespace dreamor {

template <typename... Keys>
class ValueTable;

namespace internal {
// The one way a backend's VarTable hands over its values.
struct ValueTableAccess {
  template <typename... Keys>
  [[nodiscard]] static ValueTable<Keys...> make(IndexSet<std::tuple<Keys...>> keys,
                                                std::shared_ptr<const FlatIndex<std::tuple<Keys...>>> index,
                                                std::vector<double> values) {
    return ValueTable<Keys...>{std::move(keys), std::move(index), std::move(values)};
  }
};
}  // namespace internal

// Values of a VarTable's variables, read in one go by its solution(), whatever the backend. Keyed like the
// table: yieldPlan(keys...) or yieldPlan(row), and range-for over (row, value).
template <typename... Keys>
class ValueTable {
 public:
  using Key = std::tuple<Keys...>;

  // The value of a key: yieldPlan(m, i, t) or yieldPlan(row), the columns in the declared
  // order (see index_set/key_args.hpp). Throws std::out_of_range when the set does not have
  // the key: a missing value read as 0 would pass silently for a variable at 0.
  template <typename... Args>
  [[nodiscard]] double operator()(const Args&... key) const {
    if constexpr (internal::checkTableKey<Key, Args...>()) {
      const std::size_t at = index_->find(internal::keyOf<Key>(key...), rows());
      if (at == internal::FlatIndex<Key>::kNotFound) {
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
      return index_->find(internal::keyOf<Key>(key...), rows()) != internal::FlatIndex<Key>::kNotFound;
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
  ValueTable(IndexSet<Key> keys, std::shared_ptr<const internal::FlatIndex<Key>> index, std::vector<double> values)
      : keys_{std::move(keys)}, index_{std::move(index)}, values_{std::move(values)} {}

  [[nodiscard]] const std::vector<Key>& rows() const noexcept { return internal::SetAccess::rows(keys_); }

  IndexSet<Key> keys_;
  std::shared_ptr<const internal::FlatIndex<Key>> index_;
  std::vector<double> values_;
};

}  // namespace dreamor
