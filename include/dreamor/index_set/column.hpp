// SPDX-License-Identifier: Apache-2.0
// File: column.hpp
// A column of an index set: a value type plus a name. The name is required, not inferred,
// because two columns over the same entity are the ordinary case:
//
//     using MachineCol = Col<const Machine*, "machine">;
//     using FromCol = Col<const Node*, "from">;
//     using ToCol   = Col<const Node*, "to">;
//
// Col converts implicitly to the value it wraps, so predicates, entity code and VarTable
// see a plain `const Machine*`; -> and * reach the entity directly.
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/general/tuple_hash.hpp"

namespace dreamor {

// Structural type, so it can be a template argument.
template <std::size_t N>
struct ColumnName {
  // NOLINTNEXTLINE(*-avoid-c-arrays,*-non-private-member-variables-in-classes): a structural NTTP holds the literal
  char value[N]{};

  // NOLINTNEXTLINE(*-explicit-constructor,*-avoid-c-arrays): Col<T, "name"> passes the literal straight in
  consteval ColumnName(const char (&text)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
      value[i] = text[i];
    }
  }

  [[nodiscard]] constexpr std::string_view view() const { return {static_cast<const char*>(value), N - 1}; }

  friend constexpr bool operator==(const ColumnName&, const ColumnName&) = default;
};

template <std::size_t N>
ColumnName(const char (&)[N]) -> ColumnName<N>;  // NOLINT(*-avoid-c-arrays): deduces N from the literal

template <typename T, ColumnName Name>
class Col {
 public:
  using Value = T;

  static constexpr std::string_view kName = Name.view();

  Col() = default;
  // NOLINTNEXTLINE(*-explicit-constructor): a column is its value, under a name
  Col(T value) noexcept(std::is_nothrow_move_constructible_v<T>) : value_{std::move(value)} {}

  operator T() const noexcept {  // NOLINT(*-explicit-constructor): and reads as it, m->capacity
    return value_;
  }
  [[nodiscard]] T get() const noexcept { return value_; }

  auto operator->() const noexcept
    requires std::is_pointer_v<T>
  {
    return value_;
  }
  decltype(auto) operator*() const noexcept
    requires std::is_pointer_v<T>
  {
    return *value_;
  }

  friend bool operator==(Col, Col) = default;

 private:
  T value_{};
};

// Keeps a keyName() written for the unwrapped value working.
template <typename T, ColumnName Name>
  requires requires(T value) { keyName(value); }
[[nodiscard]] auto keyName(const Col<T, Name>& column) {
  return keyName(column.get());
}

// With no hook of its own, a column names itself by the value it wraps - so an int or a
// string column needs no hook at all. An entity pointer still does: it has no formatter,
// so this yields nothing printable and VarTable rejects the key.
template <typename T, ColumnName Name>
  requires(!requires(T value) { keyName(value); })
[[nodiscard]] T keyName(const Col<T, Name>& column) {
  return column.get();
}

// Opt-in hook for callables whose columns cannot be deduced from their signature -
// anything with a template operator() constrained on argument COUNT rather than type.
// Specialize with `using type = std::tuple<NodeCol, PeriodCol>;`.
template <typename Pred>
struct PredicateColumns {};

namespace internal {

template <typename T>
struct IsColumn : std::false_type {};
template <typename T, ColumnName Name>
struct IsColumn<Col<T, Name>> : std::true_type {};

template <typename Column>
concept is_column = IsColumn<Column>::value;

// Entities, raw pointers and smart pointers all reduce to a pointer column; plain values
// are taken as they are.
template <typename Column, typename Element>
[[nodiscard]] Column toColumn(Element& element) {
  using Value = Column::Value;
  if constexpr (std::is_pointer_v<Value>) {
    if constexpr (std::is_pointer_v<std::remove_cvref_t<Element>>) {
      return Column{static_cast<Value>(element)};
    } else if constexpr (requires { element.get(); }) {
      // get() may hand back a fancy pointer when a custom deleter defines its own
      // pointer type.
      return Column{static_cast<Value>(std::to_address(element.get()))};
    } else {
      return Column{static_cast<Value>(std::addressof(element))};
    }
  } else {
    return Column{element};
  }
}

// 0 means "not a column of this set"; 2 or more means a duplicated name, which makeSet
// rejects.
template <typename Column, typename Tup, std::size_t I = 0>
[[nodiscard]] consteval std::size_t countColumns() {
  if constexpr (I >= std::tuple_size_v<Tup>) {
    return 0;
  } else {
    return (std::is_same_v<Column, std::tuple_element_t<I, Tup>> ? 1 : 0) + countColumns<Column, Tup, I + 1>();
  }
}

// Every element must be a Col, including the ones a spread source brings.
template <typename Tup, std::size_t... Is>
[[nodiscard]] consteval bool allElementsAreColumns(std::index_sequence<Is...> /*unused*/) {
  return (is_column<std::tuple_element_t<Is, Tup>> && ...);
}

// Lookup is by type, so the same column TYPE must not appear twice.
template <typename Tup, std::size_t... Is>
[[nodiscard]] consteval bool allColumnsDistinct(std::index_sequence<Is...> indices) {
  if constexpr (!allElementsAreColumns<Tup>(indices)) {
    return true;  // allElementsAreColumns reports it; do not pile on.
  } else {
    return ((countColumns<std::tuple_element_t<Is, Tup>, Tup>() == 1) && ...);
  }
}

// Distinct types are not enough: Col<const Machine*, "x"> and Col<const Order*, "x"> are
// different types with the same name, which would collide wherever kName is used - variable
// names in the model, most of all.
template <typename Tup, std::size_t... Is>
[[nodiscard]] consteval bool allColumnNamesDistinct(std::index_sequence<Is...> indices) {
  if constexpr (!allElementsAreColumns<Tup>(indices)) {
    return true;  // ::kName would not even substitute.
  } else {
    const std::array<std::string_view, sizeof...(Is)> names{std::tuple_element_t<Is, Tup>::kName...};
    for (std::size_t i = 0; i < names.size(); ++i) {
      for (std::size_t j = i + 1; j < names.size(); ++j) {
        if (names[i] == names[j]) { return false; }
      }
    }
    return true;
  }
}

template <typename Tup, typename... Columns>
consteval void checkColumns() {
  static_assert((is_column<Columns> && ...),
                "Name the column: s.groupBy<MachineCol>(), not s.groupBy<const Machine*>(). "
                "Declare it once with `using MachineCol = Col<const Machine*, \"machine\">;`.");
  static_assert(((countColumns<Columns, Tup>() != 0) && ...), "No column of this set has that name.");
  // A repeat would build a tuple holding the same column twice, and std::get<Column> on
  // that is ill-formed - so the failure would surface much later, inside <tuple>.
  static_assert(allColumnsDistinct<std::tuple<Columns...>>(std::make_index_sequence<sizeof...(Columns)>{}),
                "The same column is named twice.");
}

}  // namespace internal

}  // namespace dreamor

// TupleHash hashes a column through std::hash, so a column needs one: its value's, as hashElement
// takes it.
template <typename T, dreamor::ColumnName Name>
struct std::hash<dreamor::Col<T, Name>> {
  [[nodiscard]] std::size_t operator()(dreamor::Col<T, Name> column) const noexcept {
    return dreamor::general::hashElement(column.get());
  }
};
