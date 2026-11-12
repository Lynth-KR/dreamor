// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// How a value is written in a name: in a variable's name, in indexedName, under std::format and on a stream.
#pragma once

#include <algorithm>
#include <cstddef>
#include <format>
#include <iosfwd>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/format_buffer.hpp"
#include "dreamor/internal/base/inline.hpp"
#include "dreamor/internal/base/primary_key.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/ref.hpp"

namespace dreamor::internal::inline index_set {

// For a type that has no formatter, the standard disables std::formatter's primary template, which is then not
// default constructible. Before C++23's std::formattable, this is the standard way to check for a formatter.
template <typename T>
concept formattable = std::is_default_constructible_v<std::formatter<std::remove_cvref_t<T>, char>>;

template <typename T>
concept name_number = (std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_floating_point_v<T>;

template <typename T>
concept name_text = std::is_convertible_v<const T&, std::string_view>;

template <typename T>
struct IsNameTuple : std::false_type {};

template <typename... Ts>
struct IsNameTuple<std::tuple<Ts...>> : std::true_type {};

// The sentence of the contract that keyReachesItself() checks, in the Entities section: a primary key does not lead
// back to its own type.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_KEY_REACHES_ITSELF_MESSAGE                                                              \
  "A primary key must not lead back to its own type: key the entity by a value of its own, such as a code, and " \
  "keep what it belongs to in a set, as the bill of materials keeps its parents and children in an "             \
  "IndexSet<cParent, cChild>."

// Checks whether T, met while following the primary key of Entity, leads to Entity: through a column, a Ref, an
// element of a tuple or the key of another entity. Seen holds the entities already followed, so that a key that leads
// back to another entity, and not to Entity, ends the search; the check of that entity reports it.
template <typename Entity, typename T, typename... Seen>
[[nodiscard]] consteval bool keyLeadsTo() {
  using V = std::remove_cvref_t<T>;
  if constexpr (std::is_same_v<V, Entity>) {
    return true;
  } else if constexpr (column<V>) {
    return keyLeadsTo<Entity, typename V::value_type, Seen...>();
  } else if constexpr (ref<V>) {
    return keyLeadsTo<Entity, typename V::element_type, Seen...>();
  } else if constexpr (IsNameTuple<V>::value) {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return (keyLeadsTo<Entity, std::tuple_element_t<I, V>, Seen...>() || ...);
    }(std::make_index_sequence<std::tuple_size_v<V>>{});
  } else if constexpr (primary_keyed<V> && !name_number<V> && !name_text<V> && !(std::is_same_v<V, Seen> || ...)) {
    return keyLeadsTo<Entity, PrimaryKeyOf<V>, Seen..., V>();
  } else {
    return false;  // An entity already followed is not a column, a Ref or a tuple, so it reaches this branch.
  }
}

// Checks whether the primary key of Entity leads back to Entity, which the Entities section rules out: the name of
// such an entity would follow its key without end, and the structure exists so that nothing refers back to itself.
template <typename Entity>
[[nodiscard]] consteval bool keyReachesItself() {
  if constexpr (primary_keyed<Entity>) {
    return keyLeadsTo<Entity, PrimaryKeyOf<Entity>, Entity>();
  } else {
    return false;
  }
}

// Checks whether a value of type T has a name, with the rules in the order in which appendName() tries them. A key
// that leads back to its own type stops here, before the check would follow it without end.
template <typename T>
[[nodiscard]] consteval bool isNameable() {
  using V = std::remove_cvref_t<T>;
  if constexpr (column<V>) {
    return isNameable<typename V::value_type>();
  } else if constexpr (ref<V>) {
    return isNameable<typename V::element_type>();
  } else if constexpr (IsNameTuple<V>::value) {
    return []<std::size_t... I>(std::index_sequence<I...>) {
      return (isNameable<std::tuple_element_t<I, V>>() && ...);
    }(std::make_index_sequence<std::tuple_size_v<V>>{});
  } else if constexpr (std::is_enum_v<V> || name_number<V> || name_text<V>) {
    return true;
  } else if constexpr (primary_keyed<V>) {
    if constexpr (keyReachesItself<V>()) {
      static_assert(always_false<V>, DREAMOR_INTERNAL_KEY_REACHES_ITSELF_MESSAGE);
      return false;
    } else {
      return isNameable<PrimaryKeyOf<V>>();
    }
  } else {
    return formattable<V>;
  }
}

// A value that can be written in the name of a variable or a row, or by std::format as a column.
template <typename Key>
concept nameable = isNameable<Key>();

// The number that an enum without a std::formatter of its own is written as: its underlying value, promoted as unary
// + promotes it. An enum over char or bool is then written as a number by appendName() and by std::format alike;
// without the promotion, std::format would write an enum over char as a character.
template <typename V>
using EnumNumber = decltype(+std::declval<std::underlying_type_t<V>>());

// Appends separator and then the name of value, by the rules of the Names section. A name takes one step for each
// column, Ref, entity and key on the way to its tokens, and a call for each step costs more than writing a token. So
// the steps are member functions, which DREAMOR_INLINE forces inline with every compiler; no key leads back to its
// own type (keyReachesItself()), so the steps end.
// NOLINTBEGIN(misc-no-recursion): each step calls the next with another type, and no key leads back to its own type
struct AppendNameFn {
  template <typename Builder, typename T>
  DREAMOR_INLINE std::size_t operator()(Builder& names,
                                        std::size_t length,
                                        const T& value,
                                        std::string_view separator) const {
    using V = std::remove_cvref_t<T>;
    if constexpr (column<V>) {
      return (*this)(names, length, value.get(), separator);
    } else if constexpr (std::is_same_v<V, char>) {  // A char is written as the character, not as its number.
      const char character = value;
      return names.append(length, std::string_view{&character, 1}, separator);
    } else if constexpr (std::is_enum_v<V> && !formattable<V>) {
      return names.append(length, static_cast<EnumNumber<V>>(value), separator);
    } else if constexpr (ref<V>) {
      if (!value) { return names.append(length, std::string_view{}, separator); }
      return (*this)(names, length, *value, separator);
    } else if constexpr (IsNameTuple<V>::value) {
      return tuple(names, length, value, separator, "_");
    } else if constexpr (primary_keyed<V> && !name_number<V> && !name_text<V>) {
      if constexpr (keyReachesItself<V>()) {
        static_assert(always_false<V>, DREAMOR_INTERNAL_KEY_REACHES_ITSELF_MESSAGE);
        return length;
      } else {
        const auto& key = primaryKeyOf(value);
        if constexpr (IsNameTuple<std::remove_cvref_t<decltype(key)>>::value) {
          return tuple(names, length, key, separator, primaryKeySeparatorOf(value));
        } else {
          return (*this)(names, length, key, separator);
        }
      }
    } else {
      return names.append(length, value, separator);
    }
  }

  // Appends the elements of a tuple: the first after `separator`, and each other element after `inner`.
  template <typename Builder, typename Tuple>
  DREAMOR_INLINE std::size_t tuple(Builder& names,
                                   std::size_t length,
                                   const Tuple& parts,
                                   std::string_view separator,
                                   std::string_view inner) const {
    return std::apply(
        [&](const auto& first, const auto&... rest) {
          // NOLINTNEXTLINE(misc-const-correctness): each later part assigns it; with one part none does
          std::size_t at = (*this)(names, length, first, separator);
          ((at = (*this)(names, at, rest, inner)), ...);
          return at;
        },
        parts);
  }
};

// NOLINTEND(misc-no-recursion)

// NOLINTNEXTLINE(readability-identifier-naming): a function object, named as its function
inline constexpr AppendNameFn appendName{};

// The builder that appendName() writes to for std::format: it writes each token straight to the formatter's output.
// It has no buffer, so a name has no limit on its length. Only a name that a solver takes has a limit. A text is
// copied, and any other token is written as std::format writes it for "{}", as NameBuilder writes it.
template <typename Out>
struct FormatSink {
  Out out;

  template <typename Token>
  std::size_t append(std::size_t length, const Token& token, std::string_view separator) {
    out = std::copy(separator.begin(), separator.end(), out);
    if constexpr (std::is_convertible_v<const Token&, std::string_view>) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay): a string literal decays here
      const std::string_view text{token};
      out = std::copy(text.begin(), text.end(), out);
    } else {
      out = std::format_to(out, "{}", token);
    }
    return length + 1;  // Nothing reads the length here, so any value serves.
  }
};

// The text that an exception shows for a key that a table or an EntityVector does not have, such as
// "(M1, bolt, 3)". Each argument, and each column of a row, is written by its name, so that a misspelled name in the
// data can be found. The text is made only when the exception is thrown.
template <typename... Args>
[[nodiscard]] std::string keyText([[maybe_unused]] const Args&... key) {
  // key is unused where an argument has no name: cl.exe warns C4100 there, as on a set of a std::optional column.
  if constexpr (sizeof...(Args) == 1 && (IsNameTuple<std::remove_cvref_t<Args>>::value && ...)) {
    return std::apply([](const auto&... columns) { return keyText(columns...); }, key...);
  } else if constexpr ((nameable<Args> && ...)) {
    std::string text;
    FormatSink<std::back_insert_iterator<std::string>> sink{std::back_inserter(text)};
    std::string_view separator = "(";
    ((static_cast<void>(appendName(sink, 0, key, separator)), separator = ", "), ...);
    text += ')';
    return text;
  } else {
    return "(a key)";
  }
}

// The size of indexedName's buffer: the longest name and its null character.
inline constexpr std::size_t kIndexedNameSize = 256;

// The builder of the calling thread for indexedName. One builder serves every name, whatever the types of its keys.
[[nodiscard]] inline NameBuilder<kIndexedNameSize>& indexedNameBuilder() {
  thread_local NameBuilder<kIndexedNameSize> builder{""};
  return builder;
}

// The standard formatter that formats a value under a format spec. An integer, a char, an enum and a text use their
// own formatter, so that a spec such as {:>4}, {:04} or {:<8} means what it means for that value. For any other
// value, such as a Ref or a user's type, appendName() writes the name, and the spec applies to that text.
template <typename V>
struct SpecFormatterOf {
  using type                    = std::formatter<std::string_view, char>;
  static constexpr bool kDirect = false;
};

template <typename V>
  requires(std::is_integral_v<V> && !std::is_same_v<V, bool>) || std::is_same_v<V, std::string_view> ||
          std::is_same_v<V, std::string>
struct SpecFormatterOf<V> {
  using type                    = std::formatter<V, char>;
  static constexpr bool kDirect = true;
};

template <typename V>
  requires std::is_enum_v<V> && formattable<V>
struct SpecFormatterOf<V> {
  using type                    = std::formatter<V, char>;
  static constexpr bool kDirect = true;
};

template <typename V>
  requires std::is_enum_v<V> && (!formattable<V>)
struct SpecFormatterOf<V> {
  using type                    = std::formatter<EnumNumber<V>, char>;
  static constexpr bool kDirect = true;
};

// Formats a column's value or a Ref: by its name, and under a format spec through SpecFormatterOf. If appendName()
// writes the value and a spec is given, the name is first written into a std::string. That is the only case that
// allocates memory.
template <typename Value>
class NameFormatter {
 public:
  constexpr auto parse(std::format_parse_context& context) {
    spec_ = context.begin() != context.end() && *context.begin() != '}';
    return inner_.parse(context);
  }

  template <typename FormatContext>
  auto format(const Value& value, FormatContext& context) const {
    if constexpr (SpecFormatterOf<Value>::kDirect) {
      if constexpr (std::is_enum_v<Value> && !formattable<Value>) {
        return inner_.format(static_cast<EnumNumber<Value>>(value), context);
      } else {
        return inner_.format(value, context);
      }
    } else {
      if (!spec_) {
        FormatSink<typename FormatContext::iterator> sink{context.out()};
        static_cast<void>(appendName(sink, 0, value, ""));
        return sink.out;
      }
      std::string text;
      FormatSink<std::back_insert_iterator<std::string>> sink{std::back_inserter(text)};
      static_cast<void>(appendName(sink, 0, value, ""));
      return inner_.format(std::string_view{text}, context);
    }
  }

 private:
  SpecFormatterOf<Value>::type inner_;
  bool spec_ = false;
};

}  // namespace dreamor::internal::inline index_set

namespace dreamor {

/// @section Names
///
/// The parts of this section make names from the values that identify a row: names of variables, of constraints and
/// of other things that a solver names. A `VarTable` names each of its variables `<name>_<key0>_<key1>...`, from its
/// own name and the values of the variable's row. `indexedName` makes a name in the same way. `std::format` and an
/// output stream write a column or a `Ref` with the same text.
///
/// A value is written in a name by the first of these rules that applies to it. If a rule writes a value as another
/// value, such as a column's value or an entity's primary key, the other value is written by the same rules.
///
/// - A column is written as its value.
/// - A `char` is written as the character.
/// - An enum that has no `std::formatter` of its own is written as its underlying integer.
/// - A `Ref` is written as the entity that it refers to. An empty `Ref` is written as an empty text.
/// - A `std::tuple` is written as its elements, joined by `_`.
/// - A value whose type has a primary key, such as an entity, is written as its primary key. If the primary key is
///   a `std::tuple`, its elements are joined by the text that `primaryKeySeparator()` returns, or by `_` if the type
///   has no `primaryKeySeparator()`.
/// - An integer or a floating-point number, other than a `bool`, is written as `std::format` writes it for `{}`.
/// - A text is written as it is.
/// - Any other value, such as a `bool` or an enum that has its own `std::formatter`, is written by its
///   `std::formatter`.
///
/// If no rule applies to a value, the value has no name. A `VarTable` over a set with such a value in a column does
/// not compile. A call of `indexedName` with such a value does not compile either, and neither does `std::format` of
/// such a column or `Ref`.
///
/// A format spec, such as `{:>4}`, applies to a column in one of two ways:
///
/// - For a column of integers, of `char`, of an enum or of text, `std::format` applies the spec to the column's
///   value, as it does for the value itself.
/// - For any other column, and for a `Ref`, `std::format` writes the name first and applies the spec to that text.
///   The spec must be one that `std::format` accepts for a text.
///
/// Guide: guide/names-and-logging.md

/// Returns the name `<family>_<key0>_<key1>...` of a row, with each of `keys` written as the Names section describes.
///
/// A `VarTable` names its variables with the same text. Each of `keys` is a column, a row or another value.
///
/// The name is written into a buffer that belongs to the calling thread. If a key's `std::formatter` throws an
/// exception, the exception propagates.
///
/// Takes: Each type in `Keys` must have a name by the rules of the Names section.
/// Returns: A pointer to the name, which ends with a null character. The pointer is valid until the calling thread
/// calls `indexedName` again, so it is meant for a function that copies the name, such as a solver's function that
/// names a constraint.
/// Throws: `std::runtime_error` if the name is longer than 255 characters.
/// Cost: No memory is allocated, other than by a key's own `std::formatter`. Numbers and texts are written without
/// a format string.
template <typename... Keys>
  requires(internal::nameable<Keys> && ...)
[[nodiscard]] const char* indexedName(std::string_view family, const Keys&... keys) {
  NameBuilder<internal::kIndexedNameSize>& names = internal::indexedNameBuilder();
  // NOLINTNEXTLINE(misc-const-correctness): each key assigns it, but with no keys nothing does
  [[maybe_unused]] std::size_t length = names.restart(family);
  ((length = internal::appendName(names, length, keys, "_")), ...);
  return names.name();
}

/// Writes the name of `value` to `out`.
///
/// The text is the one that `std::format` writes for `value` with the format string `{}`.
///
/// Takes: `T` must be a column or a `Ref`. `T` must have a name by the rules of the Names section.
/// Returns: A reference to `out`.
template <typename Traits, typename T>
  requires(internal::column<T> || internal::ref<T>) && internal::nameable<T>
std::basic_ostream<char, Traits>& operator<<(std::basic_ostream<char, Traits>& out, const T& value) {
  return out << std::format("{}", value);
}

}  // namespace dreamor

// std::format writes a column by its name. Under a format spec, it formats the column as NameFormatter describes.
// NOLINTBEGIN(bugprone-std-namespace-modification): only for program-defined types, a column and a Ref
template <typename C>
  requires dreamor::internal::column<C> && dreamor::internal::nameable<C>
struct std::formatter<C, char> : dreamor::internal::NameFormatter<typename C::value_type> {
  template <typename FormatContext>
  auto format(const C& column, FormatContext& context) const {
    return dreamor::internal::NameFormatter<typename C::value_type>::format(column.get(), context);
  }
};

// std::format writes a Ref by the primary key of its entity, as it writes a column of entities.
template <typename T>
  requires dreamor::internal::nameable<dreamor::Ref<T>>
struct std::formatter<dreamor::Ref<T>, char> : dreamor::internal::NameFormatter<dreamor::Ref<T>> {};

// NOLINTEND(bugprone-std-namespace-modification)
