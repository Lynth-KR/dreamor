// SPDX-License-Identifier: Apache-2.0
// File: internal/var_table_core.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// The backend-free part of a variable table: the set it is built on, the index from key to
// row, the variables in the set's order, lookup and iteration. A backend's VarTable derives
// from VarTableCore<Var, Keys...>, creates the variables into vars_ in its constructor, and
// adds operator() (the variable of a row) and solution() in its own solver's terms.
#pragma once

#include <concepts>
#include <cstddef>
#include <format>  // IWYU pragma: keep // For std::formatter
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/general/format_buffer.hpp"
#include "dreamor/index_set/call.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"
#include "dreamor/internal/row_index.hpp"
#include "dreamor/value_table.hpp"

namespace dreamor {

// Customization point for how a key column appears inside generated variable names.
//
// Define a free function `keyName(key)` in the SAME NAMESPACE as the key type; it is picked
// up by ADL. It must return something std::format can print. Example, next to Equipment:
//
//     inline int keyName(const Equipment* e) { return e->eqid; }
//
// A column over a value std::format already handles - an int period, a string grade - needs
// no hook; column.hpp names it by that value. An entity pointer has no formatter, so it
// does need one. Prefer the hook over specializing std::formatter for the pointer type: a
// std::formatter specialization is global and would rewrite every unrelated
// std::format("{}", ptr) that meant to print an address.
namespace internal {

// std::formatter's primary template is disabled (not default constructible) for types that
// have no formatter, which is the standard way to detect formattability before C++23's
// std::formattable.
template <typename T>
concept is_formattable = std::is_default_constructible_v<std::formatter<std::remove_cvref_t<T>, char>>;

template <typename Key>
concept has_key_name = requires(const Key& key) {
  { keyName(key) } -> is_formattable;  // found via ADL
};

template <typename Key>
concept is_nameable = has_key_name<Key> || is_formattable<Key>;

// Returns the token to embed in a variable name: the ADL hook if present, else the key.
template <typename Key>
[[nodiscard]] decltype(auto) toNameToken(const Key& key) {
  if constexpr (has_key_name<Key>) {
    return keyName(key);
  } else {
    return key;
  }
}

}  // namespace internal

namespace internal {

// A bound is a number, or a callable over the row's columns.
template <typename Bound, typename Row>
consteval void checkBound() {
  if constexpr (!std::is_arithmetic_v<Bound>) {
    checkColumnCallable<Bound, Row>();
  }
}
template <typename Bound, typename Row>
[[nodiscard]] double boundOf(const Bound& bound, const Row& row) {
  if constexpr (std::is_arithmetic_v<Bound>) {
    return static_cast<double>(bound);
  } else {
    return static_cast<double>(callColumnsOf(bound, row));
  }
}

template <typename Var, typename... Keys>
  requires(sizeof...(Keys) > 0 && (is_nameable<Keys> && ...))
class VarTableCore {
  static_assert(allColumnsDistinct<std::tuple<Keys...>>(std::index_sequence_for<Keys...>{}),
                "VarTable<ICol, ICol> names one key column twice, and no set has the same column twice: give each "
                "key its own column type.");

 public:
  // The index tuple. Identical to the element type of the set the table is built on, so a
  // row from that set, a subset of it or one of its groups goes straight into operator().
  using Key = std::tuple<Keys...>;

  // contains(m, i, t) or contains(row): the key's columns in order, or a row of the set (see
  // index_set/key_args.hpp).
  template <typename... Args>
  [[nodiscard]] bool contains(const Args&... key) const {
    if constexpr (checkTableKey<Key, Args...>()) {
      return index_->find(rows(), key...) != RowIndex<Key>::kNotFound;
    } else {
      return false;
    }
  }

  [[nodiscard]] std::size_t size() const noexcept { return vars_.size(); }

  // The set this table was built on - its keys, in order.
  [[nodiscard]] const IndexSet<Key>& keys() const noexcept { return keys_; }

  // (row, variable) pairs in the set's order - the column order the model gets.
  [[nodiscard]] RowPairIterator<Key, Var> begin() const noexcept { return {rows().data(), vars_.data()}; }
  [[nodiscard]] RowPairIterator<Key, Var> end() const noexcept {
    return {rows().data() + rows().size(), vars_.data() + vars_.size()};
  }

  // Copy is deleted: a variable is a handle, so a copy would quietly alias the same columns
  // under two names. Move is fine - nothing here points at itself.
  VarTableCore(const VarTableCore&)            = delete;
  VarTableCore& operator=(const VarTableCore&) = delete;

  // 100 * vYield - what a user reaches for to scale a sum - stops on the fix rather than on a
  // list of every operator* in scope. Hidden friends, so they are candidates only when a table
  // is an operand - taken as the backend's own type, since it inherits this privately; the
  // return type is deduced so that the body, the static_assert, is instantiated as soon as
  // one is chosen.
  template <typename Scale, typename Table>
    requires std::is_base_of_v<VarTableCore, Table>  // A backend's table; the base is not accessible
  friend auto operator*(const Scale& /*scale*/, const Table& /*table*/) {
    static_assert(
        always_false<Scale>,
        "Scale the sum, not the table: 100 * sumOf(env, rows, vYield).build() (sumOf(rows, vYield) on MathOpt), or "
        "multiply inside a term. A VarTable is a family of variables, not an expression.");
    return false;
  }
  template <typename Table, typename Scale>
    requires(std::is_base_of_v<VarTableCore, Table> && !std::is_base_of_v<VarTableCore, Scale>)
  friend auto operator*(const Table& /*table*/, const Scale& /*scale*/) {
    static_assert(
        always_false<Scale>,
        "Scale the sum, not the table: 100 * sumOf(env, rows, vYield).build() (sumOf(rows, vYield) on MathOpt), or "
        "multiply inside a term. A VarTable is a family of variables, not an expression.");
    return false;
  }

 protected:
  explicit VarTableCore(const IndexSet<Key>& keys) : keys_{keys}, index_{sharedIndex(keys_)} {}

  VarTableCore(VarTableCore&&) noexcept            = default;
  VarTableCore& operator=(VarTableCore&&) noexcept = default;
  ~VarTableCore() noexcept = default;  // Solver objects follow their own owner; nothing is ended here.

  // The row of a key from an operator() call, or throws std::out_of_range - the rule of
  // every backend: a row outside the set is a modeling error, and a null handle or a 0 would
  // only surface later. Callers check the arguments with checkTableKey first.
  template <typename... Args>
  [[nodiscard]] std::size_t rowOf(const Args&... key) const {
    const std::size_t at = index_->find(rows(), key...);
    if (at == RowIndex<Key>::kNotFound) {
      throw std::out_of_range("VarTable: this tuple is not in the table's set.");
    }
    return at;
  }

  [[nodiscard]] const std::vector<Key>& rows() const noexcept { return SetAccess::rows(keys_); }

  // The values of vars_, in the same order, as a ValueTable sharing this table's set and index.
  [[nodiscard]] ValueTable<Keys...> valueTable(std::vector<double> values) const {
    return ValueTableAccess::make<Keys...>(keys_, index_, std::move(values));
  }

  // One shared buffer for the whole construction: the prefix stays put and only the suffix
  // is rewritten per key, so no per-variable allocation for names.
  //
  // kLogAndClear (not kThrow): construction is reached from noexcept modeling code, where a
  // throw would mean std::terminate rather than a recoverable error.
  static constexpr std::size_t kNameBufferSize = 256;  // a variable's name, terminator included
  using NameBuilder =
      ::dreamor::general::NameBuilder<kNameBufferSize, ::dreamor::general::OnFormatOverflow::kLogAndClear>;

  [[nodiscard]] static const char* nameOf(NameBuilder& names, const Key& key) {
    if (!names.enabled()) {
      return nullptr;  // Unnamed: no key to read or format per variable.
    }
    std::size_t length = names.rootLength();
    std::apply([&](const auto&... columns) { ((length = names.append(length, toNameToken(columns))), ...); }, key);
    return names.name();
  }

  // NOLINTNEXTLINE(*-non-private-member-variables-in-classes): the backends deriving from this fill it
  std::vector<Var> vars_;  // vars_[i] is the variable of row i.

 private:
  // The set's row index: every table on the set - vYield and vSetup on sPRODUCE - and every
  // value table from them share one, made by the first. Its hash waits for the first lookup that
  // needs one (row_index.hpp).
  [[nodiscard]] static std::shared_ptr<const RowIndex<Key>> sharedIndex(const IndexSet<Key>& keys) {
    return SetAccess::indexSlot(keys).template get<RowIndex<Key>>(
        [&keys] { return std::make_shared<const RowIndex<Key>>(SetAccess::rows(keys)); });
  }

  IndexSet<Key> keys_;                          // Shared; the only copy of the keys.
  std::shared_ptr<const RowIndex<Key>> index_;  // Shared with ValueTables.
};

}  // namespace internal
}  // namespace dreamor
