// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Columns: the struct DREAMOR_ENTITY_COLUMN or DREAMOR_VALUE_COLUMN declares, its base, and what reads and fills one.
#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/internal/base/tuple_hash.hpp"
#include "dreamor/internal/index_set/ref.hpp"
#include "dreamor/logger.hpp"

namespace dreamor {
template <typename Self, typename T>
class Col;
}  // namespace dreamor

namespace dreamor::internal::inline index_set {

// A column is the struct that DREAMOR_ENTITY_COLUMN or DREAMOR_VALUE_COLUMN declares, which derives from the Col of
// itself. Col itself is not a column, and neither is any other type that holds a value.
template <typename C>
concept column = requires {
  typename C::column_type;
  typename C::value_type;
} && std::same_as<typename C::column_type, C> && std::derived_from<C, Col<C, typename C::value_type>>;

// The integer types that std::cmp_equal accepts: every integer type except bool and the character types.
template <typename T>
concept comparable_integer =
    std::integral<T> && !std::same_as<T, bool> && !std::same_as<T, char> && !std::same_as<T, wchar_t> &&
    !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

/// @contract column_integer_in_range: An integer that fills a column of integers of another type is in the range of
/// the column's type. The integer may be the argument of the column's constructor, a value of a source, or a value
/// of an interval.

// Converts an integer to another integer type, for an interval's value or for a column that is filled from an
// integer of another type.
template <comparable_integer To, comparable_integer From>
[[nodiscard]] constexpr To integerAs(From value) noexcept {
#ifndef NDEBUG
  if (!std::in_range<To>(value)) {
    DREAMOR_DEBUG_FATAL("dreamor: a value the column's integer type cannot hold - a broken contract.");
  }
#endif
  return static_cast<To>(value);
}

// The character types that a column cannot hold, because their size differs between platforms or because a name
// cannot show them.
template <typename T>
inline constexpr bool kOtherCharacter =
    std::same_as<T, wchar_t> || std::same_as<T, char8_t> || std::same_as<T, char16_t> || std::same_as<T, char32_t>;

// The text types that a column or a table can hold: a std::string_view that refers to the data, or a std::string that
// holds a copy of the data.
template <typename T>
concept text_value = std::same_as<T, std::string_view> || std::same_as<T, std::string>;

// A false value that depends on template parameters, for the static_assert of a template that exists only to reject
// a call with a message.
template <typename...>
inline constexpr bool always_false = false;

// A value type that a set can hash with hashElement (tuple_hash.hpp): a Ref, an integer, an enum, or a type that
// std::hash accepts, such as std::string_view or a user's type with its own std::hash. The Ref case is tested first,
// so that the entity of the Ref may still be an incomplete type.
template <typename T>
concept hashable_value =
    ref<T> || std::is_integral_v<T> || std::is_enum_v<T> || std::is_default_constructible_v<std::hash<T>>;

// A value of an interval (interval.hpp), in the common type of the interval's two ends. The value is converted to the
// integer type of the column that it fills, so an interval of int values can fill a column of std::size_t.
template <std::integral T>
struct IntervalPoint {
  T value;
};

template <typename T>
struct IsIntervalPoint : std::false_type {};

template <typename T>
struct IsIntervalPoint<IntervalPoint<T>> : std::true_type {};

template <typename Value, typename T>
[[nodiscard]] Value intervalValueAs(T value) {
  static_assert(comparable_integer<Value>,
                "An interval fills a column of integers: DREAMOR_VALUE_COLUMN(cPeriod, int).");
  if constexpr (comparable_integer<Value>) {
    return integerAs<Value>(value);
  } else {
    return Value{};
  }
}

// Detects a row of one column, std::tuple<C>. A set of one column holds such rows. Such a row can also fill a
// different column of the same value type, if a step reads a set of one column into a column of another name.
template <typename T>
struct OneColumnRow : std::false_type {};

template <typename C>
  requires column<C>
struct OneColumnRow<std::tuple<C>> : std::true_type {
  using column_type = C;
};

// Converts an element of a source to the column Column:
// - A row of one column is converted as the column that it holds.
// - An entity column is filled from a Ref, from another column that holds a Ref, or from an entity of a frozen
//   EntityVector, which becomes a Ref to that entity. A chain and sumExpr accept no other source for an entity
//   column (refsFromTheirVector in element.hpp).
// - A value column is filled from an interval's value, from another column's value, from a value of its own type,
//   from any integer if the column holds integers, from any text if the column holds a std::string, or from any
//   other value that converts to its type. A debug build checks an integer of another type as it checks an
//   interval's value.
template <typename Column, typename Element>
[[nodiscard]] Column toColumn(Element& element) {
  using Value = Column::value_type;
  using Bare  = std::remove_cvref_t<Element>;
  if constexpr (OneColumnRow<Bare>::value) {
    return toColumn<Column>(std::get<0>(element));
  } else if constexpr (std::same_as<Bare, Column>) {
    return element;  // the column itself, copied once
  } else if constexpr (ref<Value>) {
    if constexpr (ref<Bare>) {
      return Column{element};
    } else if constexpr (column<Bare>) {
      return Column{element.get()};
    } else {
      return Column{RefAccess::make(std::addressof(element))};  // an element of a frozen EntityVector
    }
  } else if constexpr (IsIntervalPoint<Bare>::value) {
    return Column{intervalValueAs<Value>(element.value)};
  } else if constexpr (column<Bare>) {
    const auto& value = element.get();  // a column of another name: its value fills this one, not copied first
    return toColumn<Column>(value);
  } else if constexpr (std::same_as<Bare, Value>) {
    return Column{element};
  } else if constexpr (comparable_integer<Value> && comparable_integer<Bare>) {
    return Column{integerAs<Value>(element)};
  } else if constexpr (std::same_as<Value, std::string> && std::is_convertible_v<const Bare&, std::string_view>) {
    return Column{std::string{std::string_view{element}}};  // a std::string column holds a copy of the text
  } else {
    // A floating-point value never fills an integer column: 1.5 would silently become the row of 1.
    constexpr bool kTakes =
        std::is_convertible_v<const Bare&, Value> && !(std::is_floating_point_v<Bare> && std::integral<Value>);
    static_assert(kTakes,
                  "A column takes values of its own type, any integer for a column of integers, or text for a "
                  "column of text: convert this one on purpose.");
    if constexpr (kTakes) {
      return Column{static_cast<Value>(element)};
    } else {
      return Column{};
    }
  }
}

// The element types of a row as one std::tuple: the types of a std::tuple itself, or the types of another tuple-like
// type through tuple_element. A lookup by type over this pack is one fold expression. Without the pack, a lookup
// would instantiate a template for every position, and a set with many columns would pay that for every column that
// it looks up.
template <typename Tup, typename Is = std::make_index_sequence<std::tuple_size_v<Tup>>>
struct RowPack;

template <typename Tup, std::size_t... Is>
struct RowPack<Tup, std::index_sequence<Is...>> {
  using type = std::tuple<std::tuple_element_t<Is, Tup>...>;
};

template <typename... Ts, std::size_t... Is>
struct RowPack<std::tuple<Ts...>, std::index_sequence<Is...>> {
  using type = std::tuple<Ts...>;
};

template <typename Column, typename Pack>
inline constexpr std::size_t kCountIn = 0;
template <typename Column, typename... Ts>
inline constexpr std::size_t kCountIn<Column, std::tuple<Ts...>> =
    (std::size_t{0} + ... + static_cast<std::size_t>(std::is_same_v<Column, Ts>));

// The first position of Column among the pack's types, or the number of types if Column is not one of them.
template <typename Column, typename Pack>
inline constexpr std::size_t kIndexIn = 0;
template <typename Column, typename... Ts>
inline constexpr std::size_t kIndexIn<Column, std::tuple<Ts...>> = [] {
  constexpr std::array<bool, sizeof...(Ts)> kSame{std::is_same_v<Column, Ts>...};
  std::size_t at = 0;
  while (at < kSame.size() && !kSame[at]) {
    ++at;
  }
  return at;
}();

// Counts how often Column occurs in the row. 0 means that Column is not a column of the set. 2 or more means that a
// column occurs twice, which a chain rejects.
template <typename Column, typename Tup>
[[nodiscard]] consteval std::size_t countColumns() {
  return kCountIn<Column, typename RowPack<Tup>::type>;
}

// Checks that every element type is a column, including the columns that a spread source adds.
template <typename Tup, std::size_t... Is>
[[nodiscard]] consteval bool allElementsAreColumns(std::index_sequence<Is...> /*unused*/) {
  return (column<std::tuple_element_t<Is, Tup>> && ...);
}

// A column is found by its type, so no column type may occur twice.
template <typename Tup, std::size_t... Is>
[[nodiscard]] consteval bool allColumnsDistinct(std::index_sequence<Is...> indices) {
  if constexpr (!allElementsAreColumns<Tup>(indices)) {
    return true;  // allElementsAreColumns reports the error, and a second error would add nothing.
  } else {
    return ((countColumns<std::tuple_element_t<Is, Tup>, Tup>() == 1) && ...);
  }
}

template <typename Tup, typename... Columns>
consteval void checkColumns() {
  static_assert((column<Columns> && ...),
                "Name the column: s.groupBy<cMachine>(), not s.groupBy<Ref<Machine>>(). "
                "Declare it once with `DREAMOR_ENTITY_COLUMN(cMachine, Machine);`.");
  static_assert(((countColumns<Columns, Tup>() != 0) && ...),
                "No column of this set has that name: name the set's own columns, as its type lists them - "
                "IndexSet<cMachine, cItem> has cMachine and cItem.");
  // If a column occurred twice, the row would hold that column twice, and std::get<Column> on it is ill-formed. The
  // error would then appear later, inside <tuple>, instead of here.
  static_assert(allColumnsDistinct<std::tuple<Columns...>>(std::make_index_sequence<sizeof...(Columns)>{}),
                "The same column is named twice: name each column once - S.select<cMachine, cItem>().");
}

// The columns of one of dreamor's own column callables whose operator() is a template, which dreamor cannot read off
// a parameter list: Membership, ParamTable, SolutionTable and every VarTable take their key in several forms. A
// specialization defines `type`, a std::tuple of the columns, and takes precedence over the parameter list. A model's
// callable declares its columns in its signature instead: this trait is internal, so that a generic lambda has no way
// around that rule.
template <typename Pred>
struct CallableColumns {};

}  // namespace dreamor::internal::inline index_set

namespace dreamor {

/// @section Columns
///
/// The parts of this section declare columns. A column is a type that marks a place in a row and says what the rows
/// hold in that place. An entity column holds a `Ref` to an entity. A value column holds a plain value that
/// identifies the row by `==`, such as an integer, an enum or a text.
///
/// A model declares each column once, with `DREAMOR_ENTITY_COLUMN` or `DREAMOR_VALUE_COLUMN`. Because a column is a
/// type, two columns over the same entity, such as `cFrom` and `cTo`, are different types. A column callable and a
/// lookup refer to the columns that they read by these types. In an expression, a column converts to the value that
/// the column holds.
///
/// Guide: guide/columns.md

/// The base class of a column that holds a value of type `T`.
///
/// `Self` is the column's own struct. The members of `Col` are the members of every column. A model declares a
/// column with `DREAMOR_ENTITY_COLUMN` or `DREAMOR_VALUE_COLUMN`, not with `Col` directly.
///
/// A column holds a value of one of these types:
///
/// - A `Ref`.
/// - An integer type, an enum, or `char`.
/// - `std::string_view`, whose characters must outlive every set whose rows hold the column.
/// - `std::string`, which holds its own characters.
/// - A type of the user's that has `==` and a specialization of `std::hash`, such as a struct that holds a measured
///   value that is itself a set's member.
///
/// A default-constructed column holds a value-initialized `T`: an empty `Ref`, zero, or an empty text. Two columns of
/// the same type are equal if their values are equal.
///
/// `std::hash` is specialized for every column, and the specialization hashes the column's value. `std::hash` is also
/// specialized for a row, which is a `std::tuple` of columns. The hash of a row is the hash that a set's index uses.
///
/// Takes: `T` must be one of the types in the list above. A specialization of `std::hash` for a type of the user's
/// must be declared before the column. `T` must not be a floating-point type, a pointer, `wchar_t`, `char8_t`,
/// `char16_t` or `char32_t`, even if the type has `==` and `std::hash`.
template <typename Self, typename T>
class Col {
  static_assert(
      !std::is_pointer_v<T>,
      "A column refers to an entity by a Ref, not a pointer: DREAMOR_ENTITY_COLUMN(cMachine, Machine), "
      "filled from the machines' EntityVector - index_set::relational::from<cMachine>(machines) - or the Refs it "
      "gives (at, find).");
  static_assert(!std::is_floating_point_v<T>,
                "A column identifies its rows by ==, which a floating-point number does not keep: a quantity is "
                "data on a set - a ParamTable - or an entity's attribute, and a measured value that is itself a "
                "set's member, such as a breakpoint, goes in a type of yours with == and std::hash.");
  static_assert(!internal::kOtherCharacter<T>,
                "A column holds a char, not a wchar_t, char8_t, char16_t or char32_t: those differ between platforms "
                "or cannot be printed in a name - hold a std::string_view of the text, or its code as an integer.");
  static_assert(std::is_pointer_v<T> || std::is_floating_point_v<T> || internal::kOtherCharacter<T> ||
                    std::equality_comparable<T>,
                "A column identifies its rows by ==: it holds a Ref, an integer, an enum, a char, a text "
                "(std::string_view, std::string), or a type of yours with ==.");
  static_assert(std::is_pointer_v<T> || std::is_floating_point_v<T> || internal::kOtherCharacter<T> ||
                    !std::equality_comparable<T> || internal::hashable_value<T>,
                "A column's rows are found by their hash: a type of yours needs std::hash as well as ==, "
                "specialized before the column is declared - template <> struct std::hash<Grade> { ... };.");

 public:
  using value_type = T;
  /// The column's own type, `Self`.
  using column_type = Self;

  Col() = default;

  /// Constructs a column that holds `value`.
  ///
  /// The constructor is not `explicit`, so a value of type `T` converts to the column implicitly.
  // NOLINTNEXTLINE(*-explicit-constructor): a value converts to its column
  Col(T value) noexcept(std::is_nothrow_move_constructible_v<T>) : value_{std::move(value)} {}

  // Overload resolution prefers this constructor to Col(T) for a temporary std::string. Without it, a column of
  // std::string_view would refer to the string after the string is destroyed.

  /// @hidden - exists only to reject a temporary std::string for a column of std::string_view with a message
  template <typename String>
    requires std::same_as<T, std::string_view> && std::same_as<std::remove_cv_t<String>, std::string>
  Col(String&& /*text*/) {  // NOLINT(*-explicit-constructor,*-missing-std-forward): only rejects the call
    static_assert(internal::always_false<String>,
                  "A column of std::string_view shows text it does not hold, and this std::string is a temporary, "
                  "gone at the end of the statement: declare the column over std::string, which holds its text, "
                  "or show text that outlives the set - the record's own field.");
  }

  /// @overload
  /// Constructs a column of integers from `value`, an integer of another type.
  ///
  /// This constructor is `explicit`, so an integer of another type does not convert to the column implicitly.
  ///
  /// Takes: `T` and `I` must both be integer types other than `bool` and the character types.
  /// Contract: column_integer_in_range
  template <internal::comparable_integer I>
    requires internal::comparable_integer<T> && (!std::same_as<I, T>)
  explicit Col(I value) noexcept : value_{internal::integerAs<T>(value)} {}

  /// @overload
  /// Constructs a column from `other`, a different column that holds the same value type.
  ///
  /// This constructor is `explicit`, so a different column does not convert to this column implicitly.
  template <typename OtherSelf>
    requires(!std::same_as<OtherSelf, Self>)
  explicit Col(const Col<OtherSelf, T>& other) noexcept : value_{other.get()} {}

  /// The type in which `get()` and the implicit conversion return the column's value.
  ///
  /// - If `T` is trivially copyable, `ReadType` is `T`.
  /// - Otherwise, as for `std::string`, `ReadType` is `const T&`.
  using ReadType = std::conditional_t<std::is_trivially_copyable_v<T>, T, const T&>;

  /// Returns the value that the column holds.
  ///
  /// The conversion is implicit, so a column can be used in an expression as its value.
  operator ReadType() const noexcept {  // NOLINT(*-explicit-constructor): a column converts to its value
    return value_;
  }

  /// Returns the value that the column holds.
  ///
  /// `get()` is needed where C++ does not apply the implicit conversion, as in template argument deduction. `get()`
  /// is also needed where the column and its value type both convert to each other, as in a conditional expression
  /// `?:` whose other operand is a value.
  [[nodiscard]] ReadType get() const noexcept { return value_; }

  /// Returns the `Ref` that the column holds, so that `->` reaches a member of the entity.
  ///
  /// Takes: The column must be an entity column. On a value column, `->` does not compile.
  /// Contract: ref_not_empty
  auto operator->() const noexcept
    requires internal::ref<T>
  {
    return value_;
  }

  /// Returns the entity that the column refers to.
  ///
  /// Takes: The column must be an entity column. On a value column, `*` does not compile.
  /// Contract: ref_not_empty
  decltype(auto) operator*() const noexcept
    requires internal::ref<T>
  {
    return *value_;
  }

  friend bool operator==(const Col&, const Col&) = default;

  /// Checks whether column `a` equals column `b`, a different column that holds the same value type, such as `cFrom`
  /// and `cTo`.
  ///
  /// Returns: True if the two values are equal; false otherwise.
  template <typename OtherSelf>
    requires(!std::same_as<OtherSelf, Self>) && std::equality_comparable<T>
  [[nodiscard]] friend bool operator==(const Col& a, const Col<OtherSelf, T>& b) noexcept {
    return a.value_ == b.get();
  }

  /// Returns the order of columns `a` and `b`, which hold the same value type, by their values.
  template <typename OtherSelf>
    requires std::three_way_comparable<T>
  [[nodiscard]] friend auto operator<=>(const Col& a, const Col<OtherSelf, T>& b) noexcept {
    return a.value_ <=> b.get();
  }

  /// @overload
  /// Checks whether column `a` equals column `b` if the two columns hold integers of different types.
  ///
  /// The values are compared with `std::cmp_equal`, so the result is correct whatever the signedness of the two types.
  /// The comparison causes no conversion warning.
  ///
  /// Returns: True if the two values are equal; false otherwise.
  template <typename OtherSelf, typename U>
    requires(!std::same_as<U, T>) && internal::comparable_integer<T> && internal::comparable_integer<U>
  [[nodiscard]] friend constexpr bool operator==(const Col& a, const Col<OtherSelf, U>& b) noexcept {
    return std::cmp_equal(a.value_, b.get());
  }

  /// @overload
  /// Returns the order of columns `a` and `b` by their values if the two columns hold integers of different types.
  ///
  /// The values are ordered with `std::cmp_less`, so the result is correct whatever the signedness of the two types.
  template <typename OtherSelf, typename U>
    requires(!std::same_as<U, T>) && internal::comparable_integer<T> && internal::comparable_integer<U>
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(const Col& a, const Col<OtherSelf, U>& b) noexcept {
    if (std::cmp_less(a.value_, b.get())) { return std::strong_ordering::less; }
    if (std::cmp_less(b.get(), a.value_)) { return std::strong_ordering::greater; }
    return std::strong_ordering::equal;
  }

  /// @overload
  /// Checks whether `column` equals `value`, which is not a column.
  ///
  /// `value` may be on either side of `==`. The comparison depends on the types:
  ///
  /// - If `column` holds an integer and `value` is an integer, the two are compared with `std::cmp_equal`, whatever
  ///   their signedness.
  /// - If `column` holds a `char` and `value` is an integer, the `char` has the platform's signedness: signed on x86,
  ///   unsigned on ARM.
  /// - In every other case, the value of `column` and `value` are compared with their own `==`. A text column
  ///   compares with a string literal or a text by its characters.
  ///
  /// Takes: The value of `column` and `value` must be comparable with `==`, or both must be integers.
  /// Returns: True if the two values are equal; false otherwise.
  template <typename Column, typename U>
    requires std::same_as<Column, Self> && (!internal::column<U>) &&
             ((internal::comparable_integer<T> && internal::comparable_integer<U>) ||
              requires(const T& column_value, const U& value) {
                { column_value == value } -> std::convertible_to<bool>;
              })
  [[nodiscard]] friend constexpr bool operator==(const Column& column, const U& value) {
    // Column is a template parameter, and its constraint is checked first. A comparison of two tuples of columns finds
    // these operators by argument-dependent lookup but never uses them, so the first constraint rejects them there and
    // the constraints after it are not evaluated.
    if constexpr (internal::comparable_integer<T> && internal::comparable_integer<U>) {
      return std::cmp_equal(column.get(), value);
    } else {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): a literal decays to reach T's ==
      return static_cast<bool>(column.get() == value);
    }
  }

  /// @overload
  /// Returns the order of `column` against `value`, which is not a column.
  ///
  /// `value` may be on either side of the comparison. The order depends on the types:
  ///
  /// - If `column` holds an integer and `value` is an integer, the two are ordered with `std::cmp_less`, whatever
  ///   their signedness.
  /// - In every other case, the value of `column` and `value` are ordered with their own `<=>`.
  ///
  /// Takes: The value of `column` and `value` must be comparable with `<=>`, or both must be integers.
  template <typename Column, typename U>
    requires std::same_as<Column, Self> && (!internal::column<U>) &&
             ((internal::comparable_integer<T> && internal::comparable_integer<U>) ||
              requires(const T& column_value, const U& value) { column_value <=> value; })
  [[nodiscard]] friend constexpr auto operator<=>(const Column& column, const U& value) {
    if constexpr (internal::comparable_integer<T> && internal::comparable_integer<U>) {
      if (std::cmp_less(column.get(), value)) { return std::strong_ordering::less; }
      if (std::cmp_less(value, column.get())) { return std::strong_ordering::greater; }
      return std::strong_ordering::equal;
    } else {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): a literal decays to reach T's <=>
      return column.get() <=> value;
    }
  }

 private:
  // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization): a default column holds T{}, as std::vector<T>(n) does
  T value_{};
};

/// The base class of an entity column, whose rows hold a `Ref<Entity>`.
///
/// `DREAMOR_ENTITY_COLUMN` declares a struct derived from `EntityColumn`. An entity column refers to an entity of an
/// `EntityVector`, as a foreign key refers to a row of another table.
///
/// Takes: `Entity` may be an incomplete type where the column is declared.
template <typename Self, typename Entity>
struct EntityColumn : Col<Self, Ref<Entity>> {
  using Col<Self, Ref<Entity>>::Col;
};

/// The base class of a value column, whose rows hold a plain value of type `T`.
///
/// `DREAMOR_VALUE_COLUMN` declares a struct derived from `ValueColumn`.
template <typename Self, typename T>
struct ValueColumn : Col<Self, T> {
  using Col<Self, T>::Col;
};

/// @hidden - dreamor's own indexes find it by argument-dependent lookup
template <typename C>
  requires internal::column<C>
[[nodiscard]] constexpr std::uint64_t hashElement(const C& column) noexcept {
  // TupleHash hashes a column by the value that it holds, in 64 bits. This overload is more constrained than the
  // template internal::hashElement, so overload resolution chooses it for a column.
  using internal::hashElement;       // the overloads for integers, enums, pointers and std::hash
  return hashElement(column.get());  // argument-dependent lookup also finds the overload for a Ref
}

}  // namespace dreamor

// columnName() is a static member function, not a static data member, because a class declared inside a function
// cannot have a static data member.

/// Declares an entity column `Name`, whose rows hold a `Ref` to an entity of the type that follows `Name`.
///
/// The column is a struct derived from `EntityColumn`. The static member function `columnName()` of the column
/// returns `Name` as a `std::string_view`, which `visitOrder()` shows.
///
/// Takes: `Name` must be an identifier. The arguments after `Name` must form the entity type. The entity type may be
/// incomplete where the column is declared, and it may contain commas.
// NOLINTBEGIN(cppcoreguidelines-macro-usage): the macro declares a type under the name it is given
#define DREAMOR_ENTITY_COLUMN(Name, ...)                                                      \
  struct Name : ::dreamor::EntityColumn<Name, __VA_ARGS__> {                                  \
    using EntityColumn::EntityColumn;                                                         \
    [[nodiscard]] static constexpr ::std::string_view columnName() noexcept { return #Name; } \
  }
/// Declares a value column `Name`, whose rows hold a plain value of the type that follows `Name`.
///
/// The column is a struct derived from `ValueColumn`. The static member function `columnName()` of the column
/// returns `Name` as a `std::string_view`, which `visitOrder()` shows.
///
/// Takes: `Name` must be an identifier. The arguments after `Name` must form a value type that `Col` accepts as `T`.
/// The value type may contain commas.
#define DREAMOR_VALUE_COLUMN(Name, ...)                                                       \
  struct Name : ::dreamor::ValueColumn<Name, __VA_ARGS__> {                                   \
    using ValueColumn::ValueColumn;                                                           \
    [[nodiscard]] static constexpr ::std::string_view columnName() noexcept { return #Name; } \
  }
// NOLINTEND(cppcoreguidelines-macro-usage)

// A column in a standard unordered container hashes as its value does.
// NOLINTBEGIN(bugprone-std-namespace-modification): only for columns and rows of columns, program-defined types
template <typename C>
  requires dreamor::internal::column<C>
struct std::hash<C> {
  [[nodiscard]] std::size_t operator()(const C& column) const noexcept {
    return static_cast<std::size_t>(dreamor::hashElement(column));
  }
};

// A row of columns hashes as a set's index hashes it. The standard library has no std::hash for std::tuple. This
// specialization accepts only tuples of columns, so it depends on program-defined types, as the standard requires.
template <typename... Cs>
  requires(sizeof...(Cs) > 0 && (dreamor::internal::column<Cs> && ...))
struct std::hash<std::tuple<Cs...>> {
  [[nodiscard]] std::size_t operator()(const std::tuple<Cs...>& row) const noexcept {
    return dreamor::internal::TupleHash{}(row);
  }
};

// NOLINTEND(bugprone-std-namespace-modification)
