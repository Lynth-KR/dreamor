// SPDX-License-Identifier: Apache-2.0
// File: index_set/key_args.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// What every lookup by key takes - vYield(m, i, t), yieldPlan(m, i, t), byItemPeriod.slice(i, t),
// orderedPeriods.prev(t), in_routes(m, i): the key's columns themselves, in the declared
// order, or a whole row as a set hands it out. Nothing else - not a braced list, not a plain
// value. Col<T> converts from T implicitly, which is handy when a set is built, but at a
// lookup it would let two int columns trade places unnoticed: x(2, 1) for x(1, 2).
//
// Lookups are templates over their arguments and check them with a static_assert, so a
// wrong call stops on one sentence instead of a list of overload candidates.
#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace dreamor::internal {

template <typename...>
inline constexpr bool always_false = false;

// Catches a braced key - slice({i, t}), prev({t}) - which no template can deduce: a lookup
// takes the columns as arguments. Any braced list converts to this, but only through a
// user-defined conversion, so an overload taking it is chosen only when nothing else fits.
struct BracedKey {
  template <typename... Any>
  BracedKey(const Any&... /*unused*/) {}  // NOLINT(*-explicit-constructor): catches {i, t} to say what to write
};

// Args... are exactly Tup's columns, in order (cv and references aside).
template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isExactColumns() {
  if constexpr (sizeof...(Args) != std::tuple_size_v<Tup>) {
    return false;
  } else {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return (std::same_as<std::remove_cvref_t<std::tuple_element_t<I, std::tuple<Args...>>>,
                           std::tuple_element_t<I, Tup>> &&
              ...);
    }(std::make_index_sequence<sizeof...(Args)>{});
  }
}

// Args... is one whole row of Tup.
template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isExactRow() {
  return sizeof...(Args) == 1 && (std::same_as<std::remove_cvref_t<Args>, Tup> && ...);
}

template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isKey() {
  return isExactColumns<Tup, Args...>() || isExactRow<Tup, Args...>();
}

// A key followed by a step count, as Ordered::next(t, 2) takes it.
template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isKeyThenSteps() {
  if constexpr (sizeof...(Args) < 2) {
    return false;
  } else {
    using Last = std::remove_cvref_t<std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>>;
    if constexpr (!std::is_integral_v<Last> || std::same_as<Last, bool>) {
      return false;
    } else {
      return []<std::size_t... I>(std::index_sequence<I...>) {
        return isKey<Tup, std::tuple_element_t<I, std::tuple<Args...>>...>();
      }(std::make_index_sequence<sizeof...(Args) - 1>{});
    }
  }
}

// The key, from either form isKey() accepts.
template <typename Tup, typename... Args>
[[nodiscard]] Tup keyOf(const Args&... args) {
  if constexpr (isExactRow<Tup, Args...>()) {
    return std::get<0>(std::forward_as_tuple(args...));
  } else {
    return Tup{args...};
  }
}

// The key and the steps, from what isKeyThenSteps() accepts.
template <typename Tup, typename... Args>
[[nodiscard]] std::pair<Tup, std::ptrdiff_t> keyAndSteps(const Args&... args) {
  const auto all = std::forward_as_tuple(args...);

  constexpr std::size_t kKeyArgs = sizeof...(Args) - 1;
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::pair<Tup, std::ptrdiff_t>{
        keyOf<Tup>(std::get<I>(all)...),
        static_cast<std::ptrdiff_t>(std::get<kKeyArgs>(all)),
    };
  }(std::make_index_sequence<kKeyArgs>{});
}

// One check per kind of lookup, each with its own sentence. They return whether the call is
// right, so the caller can skip its body when it is not and the static_assert stays the only
// error.
template <typename Key, typename... Args>
consteval bool checkTableKey() {
  static_assert(isKey<Key, Args...>(),
                "Look a variable or value up by the table's key columns, in its declared order - "
                "vYield(m, i, t) for VarTable<MachineCol, ItemCol, PeriodCol> - or by a row of its set. "
                "A plain value (2, a raw pointer) is not a column: write PeriodCol{2}.");
  return isKey<Key, Args...>();
}

template <typename Key, typename... Args>
consteval bool checkGroupKey() {
  static_assert(isKey<Key, Args...>(),
                "Slice by this groupBy's key columns, in its order - byMachinePeriod.slice(m, t) for "
                "groupBy<MachineCol, PeriodCol>() - or by a key from keyAt(). A plain value (2, a raw "
                "pointer) is not a column: write PeriodCol{2}.");
  return isKey<Key, Args...>();
}

template <typename Tup, typename... Args>
consteval bool checkOrderedKey() {
  static_assert(isKey<Tup, Args...>(),
                "Name an element of the ordered set by its columns, in order - orderedPeriods.ord(t) - "
                "or by a row of the set. A plain value (2, a raw pointer) is not a column: write "
                "PeriodCol{2}.");
  return isKey<Tup, Args...>();
}

template <typename Tup, typename... Args>
consteval bool checkOrderedStep() {
  static_assert(isKey<Tup, Args...>() || isKeyThenSteps<Tup, Args...>(),
                "Name an element of the ordered set by its columns, in order, then optionally the steps - "
                "orderedPeriods.next(t), orderedPeriods.next(t, 2) - or by a row of the set. A plain value "
                "(2, a raw pointer) is not a column: write PeriodCol{2}.");
  return isKey<Tup, Args...>() || isKeyThenSteps<Tup, Args...>();
}

template <typename Tup, typename... Args>
consteval bool checkMembershipKey() {
  static_assert(isKey<Tup, Args...>(),
                "Ask membership with the set's columns, in order - in_routes(m, i) - or with a row of it. "
                "A plain value (2, a raw pointer) is not a column: write ItemCol{item}.");
  return isKey<Tup, Args...>();
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_NO_BRACES_MESSAGE                                                       \
  "Pass the key's columns as arguments, without braces: slice(i, t), prev(t), next(t, 2) - not " \
  "slice({i, t})."

}  // namespace dreamor::internal
