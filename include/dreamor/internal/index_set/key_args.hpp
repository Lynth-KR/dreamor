// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The arguments that a lookup accepts as a key, and the checks that reject a wrong key with one message. A lookup is a
// template over its arguments that a static_assert checks: a set of overloads would list every overload as a
// candidate in the error for a wrong call.
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/internal/index_set/column.hpp"

/// @section Keys
///
/// The rules of this section say which arguments a lookup accepts as a key. The lookups are `contains` of a set, a
/// `Membership`, the call operator of a `VarTable`, a `ParamTable` or a `SolutionTable`, and the lookups of a
/// `Grouped` and of an `Ordered`. A key is one of these:
///
/// - The key's columns, one argument each, in the order in which the set declares them.
/// - One row of the set.
///
/// Wherever a lookup takes a column, the lookup also takes a row of one column, which stands for that column. In the
/// place of a column, a lookup also takes an argument that can only mean that column:
///
/// - A plain value of the column's type, which is a `Ref` for an entity column.
/// - Any integer, for a column of integers.
/// - Any value that converts to `std::string_view`, such as a string literal, for a text column.
/// - A different column that holds the same type.
///
/// These arguments are accepted only if no other column of the key holds the same type. If two columns of the key
/// hold the same type, such as `cFrom` and `cTo`, a plain value or a different column in their place does not
/// compile. The columns in another order do not compile, and a key in braces does not compile either.
///
/// In the place of an entity column, the entity's primary key and the entity object do not compile. The lookup needs
/// a `Ref` to the entity, which `at` and `find` of the `EntityVector` return.
///
/// If an integer is outside the range of its column's type, no row has that key. The lookup then behaves as for any
/// key that is not in the set.
///
/// Guide: guide/columns.md

namespace dreamor::internal::inline index_set {

// Catches a key written in braces, which no template can deduce, to report it: a lookup takes the columns as separate
// arguments. Any braced list converts to BracedKey, but only through a user-defined conversion, so overload
// resolution chooses an overload that takes BracedKey only if no other overload fits.
struct BracedKey {
  template <typename... Any>
  BracedKey(const Any&... /*unused*/) {}  // NOLINT(*-explicit-constructor): catches a braced key to report it
};

// Checks whether Args are exactly the columns of Tup, in order, ignoring cv-qualifiers and references.
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

// Checks whether Args is one argument of type Tup, a whole row.
template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isExactRow() {
  return sizeof...(Args) == 1 && (std::same_as<std::remove_cvref_t<Args>, Tup> && ...);
}

// The type that the key reads from an argument: a row of one column, std::tuple<C>, stands for C.
template <typename A>
struct KeyArgOf {
  using type = A;
};

template <typename C>
  requires column<C>
struct KeyArgOf<std::tuple<C>> {
  using type = C;
};

template <typename A>
using KeyArg = KeyArgOf<std::remove_cvref_t<A>>::type;

// How many columns of a key hold Value.
template <typename Value, typename Tup>
inline constexpr std::size_t kHoldersIn = 0;
template <typename Value, typename... Cs>
inline constexpr std::size_t kHoldersIn<Value, std::tuple<Cs...>> =
    (std::size_t{0} + ... + static_cast<std::size_t>(std::is_same_v<Value, typename Cs::value_type>));

// Checks whether an argument can fill the place of Column in Key. The column itself always can. If no other column
// of the key holds the same type, an argument that can mean only Column can too: a value of the column's type, any
// integer for a column of integers, any text for a text column, or a different column of the same value type.
template <typename Column, typename Key, typename A>
[[nodiscard]] consteval bool fillsKeyColumn() {
  using Arg   = KeyArg<A>;
  using Value = Column::value_type;
  if constexpr (std::same_as<Arg, Column>) {
    return true;
  } else if constexpr (kHoldersIn<Value, Key> != 1) {
    return false;
  } else if constexpr (column<Arg>) {
    return std::same_as<typename Arg::value_type, Value>;
  } else {
    return std::same_as<Arg, Value> || (comparable_integer<Value> && comparable_integer<Arg>) ||
           (text_value<Value> && std::is_convertible_v<const Arg&, std::string_view>);
  }
}

// Checks whether each of Args fills the column of Tup at the same position.
template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isColumnsOrValues() {
  if constexpr (sizeof...(Args) != std::tuple_size_v<Tup>) {
    return false;
  } else {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return (fillsKeyColumn<std::tuple_element_t<I, Tup>, Tup, std::tuple_element_t<I, std::tuple<Args...>>>() && ...);
    }(std::make_index_sequence<sizeof...(Args)>{});
  }
}

template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isKey() {
  return isColumnsOrValues<Tup, Args...>() || isExactRow<Tup, Args...>();
}

// Checks whether a table can use the arguments without converting them: the key's columns, or one row. A table
// finds a row of its own set by the row's address.
template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isExactKey() {
  return isExactColumns<Tup, Args...>() || isExactRow<Tup, Args...>();
}

// Checks whether the value of an argument is in the range of its column's type. Only an integer of another type can
// be out of range, such as -1 for a column of std::uint32_t. No set holds a value that the column's type cannot hold.
template <typename Column, typename A>
[[nodiscard]] bool keyArgFits(const A& arg) noexcept {
  using Arg   = KeyArg<A>;
  using Value = Column::value_type;
  if constexpr (comparable_integer<Arg> && comparable_integer<Value> && !std::same_as<Arg, Value>) {
    return std::in_range<Value>(arg);
  } else {
    return true;
  }
}

// Converts an argument to its column: the column itself, the column of a row of one column, or a column made from a
// value or from the value of another column. An integer argument has already passed keyArgFits().
template <typename Column, typename A>
[[nodiscard]] Column keyColumnOf(const A& arg) {
  using Arg   = KeyArg<A>;
  using Value = Column::value_type;
  if constexpr (!std::same_as<std::remove_cvref_t<A>, Arg>) {
    return keyColumnOf<Column>(std::get<0>(arg));
  } else if constexpr (std::same_as<Arg, Column>) {
    return arg;
  } else if constexpr (column<Arg>) {
    return Column{arg.get()};
  } else if constexpr (std::same_as<Arg, Value>) {
    return Column{arg};
  } else {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): a literal converts to std::string_view
    return Column{static_cast<Value>(arg)};
  }
}

// Checks whether A is a step count: an integer, or a column of integers.
template <typename A>
[[nodiscard]] consteval bool isSteps() {
  if constexpr (column<A>) {
    return comparable_integer<typename A::value_type>;
  } else {
    return std::is_integral_v<A> && !std::same_as<A, bool>;
  }
}

// Checks whether Args are a key followed by a step count, as the neighbour lookups of Ordered accept them.
template <typename Tup, typename... Args>
[[nodiscard]] consteval bool isKeyThenSteps() {
  if constexpr (sizeof...(Args) < 2) {
    return false;
  } else {
    using Last = std::remove_cvref_t<std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>>;
    if constexpr (!isSteps<Last>()) {
      return false;
    } else {
      return []<std::size_t... I>(std::index_sequence<I...>) {
        return isKey<Tup, std::tuple_element_t<I, std::tuple<Args...>>...>();
      }(std::make_index_sequence<sizeof...(Args) - 1>{});
    }
  }
}

// Builds the key from either form that isKey() accepts. Returns nullopt if a value is outside the range of its
// column's type, because no set has that key.
template <typename Tup, typename... Args>
[[nodiscard]] std::optional<Tup> keyOf(const Args&... args) {
  if constexpr (isExactRow<Tup, Args...>()) {
    return std::get<0>(std::forward_as_tuple(args...));
  } else {
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> std::optional<Tup> {
      if (!(keyArgFits<std::tuple_element_t<I, Tup>>(args) && ...)) { return std::nullopt; }
      return Tup{keyColumnOf<std::tuple_element_t<I, Tup>>(args)...};
    }(std::index_sequence_for<Args...>{});
  }
}

// Calls find with arguments that isKey() accepts. If the arguments are the key's columns or one row, they are
// passed unchanged, so that a row of the set is still found by its address. Otherwise each argument is converted
// to its column. Returns not_found if a value is outside the range of its column's type.
template <typename Tup, typename Find, typename... Args>
[[nodiscard]] std::size_t findKey(const Find& find, std::size_t not_found, const Args&... args) {
  if constexpr (isExactKey<Tup, Args...>()) {
    return find(args...);
  } else {
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
      if (!(keyArgFits<std::tuple_element_t<I, Tup>>(args) && ...)) { return not_found; }
      return find(keyColumnOf<std::tuple_element_t<I, Tup>>(args)...);
    }(std::index_sequence_for<Args...>{});
  }
}

// A step count as a direction and a number of steps. The number is unsigned and as wide as any integer, so that a count
// of any integer type is held as it is: the most negative std::ptrdiff_t, and an unsigned count above PTRDIFF_MAX.
struct Steps {
  bool backward        = false;
  std::uintmax_t count = 1;
};

// Returns a step count from an integer or from a column of integers.
template <typename A>
[[nodiscard]] Steps stepsOf(const A& steps) {
  const auto value = [&steps] {
    if constexpr (column<A>) {
      return steps.get();
    } else {
      return steps;
    }
  }();
  if constexpr (std::is_signed_v<decltype(value)>) {
    // 0 - the value, in unsigned arithmetic, which wraps: the most negative value has no positive counterpart.
    if (value < 0) { return {.backward = true, .count = std::uintmax_t{0} - static_cast<std::uintmax_t>(value)}; }
  }
  return {.backward = false, .count = static_cast<std::uintmax_t>(value)};
}

// Splits arguments that isKeyThenSteps() accepts into the key and the step count.
template <typename Tup, typename... Args>
[[nodiscard]] std::pair<std::optional<Tup>, Steps> keyAndSteps(const Args&... args) {
  const auto all = std::forward_as_tuple(args...);

  constexpr std::size_t kKeyArgs = sizeof...(Args) - 1;
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return std::pair<std::optional<Tup>, Steps>{
        keyOf<Tup>(std::get<I>(all)...),
        stepsOf(std::get<kKeyArgs>(all)),
    };
  }(std::make_index_sequence<kKeyArgs>{});
}

// Two common mistakes in the place of an entity column, each reported with a message of its own: the entity's
// primary key as a text, and the entity object itself. The column holds a Ref to the entity.
template <typename Column, typename A>
[[nodiscard]] consteval bool isKeyTextForEntity() {
  using Arg = KeyArg<A>;
  if constexpr (ref<typename Column::value_type>) {
    return !column<Arg> && !ref<Arg> && std::is_convertible_v<const Arg&, std::string_view>;
  } else {
    return false;
  }
}

template <typename Column, typename A>
[[nodiscard]] consteval bool isEntityForRef() {
  if constexpr (ref<typename Column::value_type>) {
    return std::same_as<KeyArg<A>, typename Column::value_type::element_type>;
  } else {
    return false;
  }
}

template <typename Tup, typename... Args>
[[nodiscard]] consteval bool anyKeyTextForEntity() {
  if constexpr (sizeof...(Args) != std::tuple_size_v<Tup>) {
    return false;
  } else {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return (isKeyTextForEntity<std::tuple_element_t<I, Tup>, std::tuple_element_t<I, std::tuple<Args...>>>() || ...);
    }(std::make_index_sequence<sizeof...(Args)>{});
  }
}

template <typename Tup, typename... Args>
[[nodiscard]] consteval bool anyEntityForRef() {
  if constexpr (sizeof...(Args) != std::tuple_size_v<Tup>) {
    return false;
  } else {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return (isEntityForRef<std::tuple_element_t<I, Tup>, std::tuple_element_t<I, std::tuple<Args...>>>() || ...);
    }(std::make_index_sequence<sizeof...(Args)>{});
  }
}

// Checks whether the arguments make one of those two mistakes. If they do, the static_assert of the mistake fails,
// and the caller's general check passes, so that the message of the mistake is the only error.
template <typename Tup, typename... Args>
consteval bool keyMistake() {
  static_assert(!anyKeyTextForEntity<Tup, Args...>(),
                "A column of entities takes a Ref to the entity, not its primary key: look the entity up - "
                "vMake(machines.at(\"M1\"), i, t).");
  static_assert(!anyEntityForRef<Tup, Args...>(),
                "A column of entities takes a Ref to the entity, not the entity object: take its Ref from the "
                "EntityVector - machines.ref(m), or walk machines.refs() or a set's rows.");
  return anyKeyTextForEntity<Tup, Args...>() || anyEntityForRef<Tup, Args...>();
}

// One check for each kind of lookup, each with its own message. Each check returns whether the call is correct, so
// that the caller can skip its body if the call is wrong. The static_assert then stays the only error.
template <typename Key, typename... Args>
consteval bool checkTableKey() {
  constexpr bool kMistake = keyMistake<Key, Args...>();
  static_assert(kMistake || isKey<Key, Args...>(),
                "Look a variable or value up by the table's key columns, in its declared order - "
                "vYield(m, i, t) for VarTable<cMachine, cItem, cPeriod> - or by a row of its set; a value goes "
                "in a column's place where its type is that column's alone in the key, and where two key "
                "columns hold one type, name each value's column: cFrom{a}, cTo{b}.");
  return isKey<Key, Args...>();
}

template <typename Key, typename... Args>
consteval bool checkGroupKey() {
  constexpr bool kMistake = keyMistake<Key, Args...>();
  static_assert(kMistake || isKey<Key, Args...>(),
                "Slice by this groupBy's key columns, in its order - byMachinePeriod.slice(m, t) for "
                "groupBy<cMachine, cPeriod>() - or by a group's key, Group::key; a value goes in a column's "
                "place where its type is that column's alone in the key, and where two key columns hold one "
                "type, name each value's column: cFrom{a}, cTo{b}.");
  return isKey<Key, Args...>();
}

template <typename Tup, typename... Args>
consteval bool checkOrderedKey() {
  constexpr bool kMistake = keyMistake<Tup, Args...>();
  static_assert(kMistake || isKey<Tup, Args...>(),
                "Name an element of the ordered set by its columns, in order - orderedPeriods.positionOf(t) - "
                "or by a row of the set; a value goes in a column's place where its type is that column's "
                "alone in the set, and where two columns hold one type, name each value's column: cFrom{a}, "
                "cTo{b}.");
  return isKey<Tup, Args...>();
}

template <typename Key, typename... Args>
consteval bool checkOrderedGroup() {
  constexpr bool kMistake = keyMistake<Key, Args...>();
  static_assert(kMistake || isKey<Key, Args...>(),
                "Name the group by the columns orderedWithin<Columns...>() groups by, in order - "
                "periodsOfMachine.first(m) for orderedWithin<cMachine>(); a value goes in a column's place "
                "where its type is that column's alone among them, and where two of them hold one type, name "
                "each value's column: cFrom{a}, cTo{b}.");
  return isKey<Key, Args...>();
}

template <typename Tup, typename... Args>
consteval bool checkOrderedStep() {
  constexpr bool kMistake = keyMistake<Tup, Args...>();
  static_assert(kMistake || isKey<Tup, Args...>() || isKeyThenSteps<Tup, Args...>(),
                "Name an element of the ordered set by its columns, in order, then optionally the steps, an "
                "integer or a column of one - orderedPeriods.next(t), orderedPeriods.next(t, 2) - or by a row of "
                "the set; a value goes in a column's place where its type is that column's alone in the set, and "
                "where two columns hold one type, name each value's column: cFrom{a}, cTo{b}.");
  return isKey<Tup, Args...>() || isKeyThenSteps<Tup, Args...>();
}

template <typename Tup, typename... Args>
consteval bool checkMembershipKey() {
  constexpr bool kMistake = keyMistake<Tup, Args...>();
  static_assert(kMistake || isKey<Tup, Args...>(),
                "Ask membership with the set's columns, in order - in_routes(m, i), sROUTES.contains(m, i) - or "
                "with a row of it; a value goes in a column's place where its type is that column's alone in "
                "the set, and where two columns hold one type, name each value's column: cFrom{a}, cTo{b}.");
  return isKey<Tup, Args...>();
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_NO_BRACES_MESSAGE                                                       \
  "Pass the key's columns as arguments, without braces: slice(i, t), prev(t), next(t, 2) - not " \
  "slice({i, t})."

}  // namespace dreamor::internal::inline index_set
