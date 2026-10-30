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
// row, the variables in the set's order, lookup (operator()) and iteration, the constructor that
// rejects a set with other key columns, and the traits that make a table a sum term. A backend's
// VarTable derives from VarTableCore<Var, Keys...> publicly, inherits its constructors
// (`using Core::Core;`), creates the variables into vars_ in its own constructor, and adds
// solution() - each in its solver's own terms.
#pragma once

#include <concepts>
#include <cstddef>
#include <exception>
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
#include "dreamor/index_set/name_token.hpp"
#include "dreamor/internal/row_index.hpp"
#include "dreamor/logger/logger_light.hpp"
#include "dreamor/value_table.hpp"

namespace dreamor::internal {

// A bound is a number, or a callable over the row's columns.
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

// What every backend's VarTable derives from, through VarTableCore: the traits below find a
// table by it.
struct VarTableBase {};

template <typename Var, typename... Keys>
  requires(sizeof...(Keys) > 0 && (is_nameable<Keys> && ...))
class VarTableCore : public VarTableBase {
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

  // A set whose columns are not exactly the keys, in order, stops here on one sentence rather
  // than on "no matching constructor". Public, as a backend inherits it (`using Core::Core;`)
  // with this access; its first parameter is the backend's environment or model.
  template <typename Model, typename OtherTup, typename... Rest>
    requires(!std::same_as<OtherTup, Key>)
  VarTableCore(Model&& /*model*/, const IndexSet<OtherTup>& /*set*/, Rest&&... /*rest*/)
      : VarTableCore{IndexSet<Key>{}} {
    static_assert(always_false<OtherTup>,
                  "A VarTable is built on a set with exactly its key columns, in the same order: "
                  "VarTable<MachineCol, ItemCol, PeriodCol> takes a SetOf<MachineCol, ItemCol, PeriodCol>. "
                  "Declare the table in the set's order, or derive a set in the table's order with select<...>().");
  }

  // The variable of a key: vYield(m, i, t) - Yield[m,i,t] in AMPL - or vYield(row). The columns
  // themselves, in the declared order; anything else stops on one sentence (see
  // index_set/key_args.hpp). Throws std::out_of_range when the set does not have the key - the
  // rule of every backend; contains() asks first.
  template <typename... Args>
  [[nodiscard]] Var operator()(const Args&... key) const {
    if constexpr (checkTableKey<Key, Args...>()) {
      return vars_[rowOf(key...)];
    } else {
      std::terminate();
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
    if (at == RowIndex<Key>::kNotFound) { throw std::out_of_range("VarTable: this tuple is not in the table's set."); }
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

}  // namespace dreamor::internal

// A table names its key columns, so it is itself a sum term: sumOf(env, rows, vYield) sums its
// variables over the rows - sum {(m,i,t) in rows} Yield[m,i,t] - without a lambda that only looks
// them up. Every backend's table, through VarTableBase.
template <typename Table>
  requires std::derived_from<Table, dreamor::internal::VarTableBase>
struct dreamor::PredicateColumns<Table> {
  using type = Table::Key;
};
// Given a row of its own set, a table finds the variable by the row's address.
template <typename Table>
  requires std::derived_from<Table, dreamor::internal::VarTableBase>
struct dreamor::internal::TakesWholeRow<Table> : std::true_type {};
