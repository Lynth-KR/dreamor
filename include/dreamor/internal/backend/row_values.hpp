// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// RowValues, one value for each row of a set, in the order of the rows, found by key through the row index that every
// table on the set shares. SolutionTable and ParamTable are both built on it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/backend/row_index.hpp"
#include "dreamor/internal/index_set/indexed.hpp"

namespace dreamor::internal::inline backend {

// The type in which a value is stored: the value's own type, except that a bool is stored as a byte. A
// std::vector<bool> packs its values into bits, and a bit has no address for a cursor to walk.
template <typename T>
using StoredValue = std::conditional_t<std::is_same_v<T, bool>, std::uint8_t, T>;

// The cursor that walks the values beside the rows: a pointer, or for bool a cursor that reads each byte as a bool.
template <typename T>
struct ValueCursor {
  using type = const T*;
};

class BoolCursor {
 public:
  BoolCursor() = default;

  explicit BoolCursor(const std::uint8_t* at) noexcept : at_{at} {}

  [[nodiscard]] bool operator*() const noexcept { return *at_ != 0; }

  BoolCursor& operator++() noexcept {
    ++at_;
    return *this;
  }

 private:
  const std::uint8_t* at_ = nullptr;
};

template <>
struct ValueCursor<bool> {
  using type = BoolCursor;
};

template <typename T, typename... Keys>
class RowValues {
 public:
  using key_type = std::tuple<Keys...>;
  using Iterator = RowPairIterator<key_type, typename ValueCursor<T>::type>;

  static constexpr std::size_t kNotFound = RowIndex<key_type>::kNotFound;

  RowValues(IndexSet<Keys...> keys, std::shared_ptr<const RowIndex<key_type>> index, std::vector<StoredValue<T>> values)
      : keys_{std::move(keys)}, index_{std::move(index)}, values_{std::move(values)} {}

  // Returns the position of a key's value, or kNotFound if the set does not have the key. The key is the columns in
  // the declared order, or one row (internal/index_set/key_args.hpp).
  template <typename... Args>
  [[nodiscard]] std::size_t find(const Args&... key) const {
    return index_->find(rows(), key...);
  }

  // Returns a bool by value, and any other value by reference, so that reading a std::string does not copy it.
  [[nodiscard]] std::conditional_t<std::is_same_v<T, bool>, bool, const T&> at(std::size_t position) const noexcept {
    if constexpr (std::is_same_v<T, bool>) {
      return static_cast<bool>(values_[position]);
    } else {
      return values_[position];
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

  [[nodiscard]] const IndexSet<Keys...>& keys() const noexcept { return keys_; }

  [[nodiscard]] Iterator begin() const noexcept { return {rows().data(), cursor(0)}; }

  [[nodiscard]] Iterator end() const noexcept { return {rows().data() + rows().size(), cursor(values_.size())}; }

 private:
  [[nodiscard]] const Rows<key_type>& rows() const noexcept { return SetAccess::rows(keys_); }

  [[nodiscard]] ValueCursor<T>::type cursor(std::size_t position) const noexcept {
    return typename ValueCursor<T>::type{values_.data() + position};
  }

  IndexSet<Keys...> keys_;
  std::shared_ptr<const RowIndex<key_type>> index_;
  std::vector<StoredValue<T>> values_;
};

}  // namespace dreamor::internal::inline backend
