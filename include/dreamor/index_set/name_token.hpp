// SPDX-License-Identifier: Apache-2.0
// File: name_token.hpp
// The token a column contributes to a name: what VarTable writes for each key in
// "<name>_<key0>_<key1>...", and what a column prints as under std::format, so that a row can
// be named the way its variables are (from a FormatBuffer, or std::format):
//
//     const FromCol i{&n1};
//     const ToCol j{&n2};
//     std::format("Cap_{}_{}", i, j)  // "Cap_1_2" with keyName(const Node*) returning the id
//
// A column whose value has no token - an entity pointer with neither keyName() nor
// primaryKey() - has no formatter, so formatting it does not compile.
#pragma once

#include <format>
#include <type_traits>

#include "dreamor/index_set/column.hpp"
#include "dreamor/logger/logger_light.hpp"

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
namespace dreamor::internal {

// std::formatter's primary template is disabled (not default constructible) for types that
// have no formatter, which is the standard way to detect formattability before C++23's
// std::formattable.
template <typename T>
concept is_formattable = std::is_default_constructible_v<std::formatter<std::remove_cvref_t<T>, char>>;

template <typename Key>
concept has_key_name = requires(const Key& key) {
  { keyName(key) } -> is_formattable;  // found via ADL
};

// An entity pointer column with no keyName() of its own, whose entity has a primary key
// (EntityVector): the key names it - a name identifies a variable, as the key does the entity.
template <typename Key>
concept has_primary_key_name = requires(const Key& key) {
  requires std::is_pointer_v<typename Key::Value>;
  { primaryKey(*key.get()) } -> is_formattable;  // found via ADL
};

template <typename Key>
concept is_nameable = has_key_name<Key> || has_primary_key_name<Key> || is_formattable<Key>;

// Returns the token to embed in a name (a variable's, or a column under std::format): the ADL hook if present, else the
// entity's primary key, else the key. A debug build says once per column when the primary key stands in.
template <typename Key>
[[nodiscard]] decltype(auto) toNameToken(const Key& key) {
  if constexpr (has_key_name<Key>) {
    return keyName(key);
  } else if constexpr (has_primary_key_name<Key>) {
    [[maybe_unused]] static const bool said = [] {
      DREAMOR_DEBUG_INFO("Column \"{}\" has no keyName(), so its entities' primaryKey() names it.", Key::kName);
      return true;
    }();
    return primaryKey(*key.get());
  } else {
    return key;
  }
}

// A column that has a token: a keyName() hook, an entity's primaryKey(), or a value
// std::format prints. (is_nameable also takes a formattable column, which is this one.)
template <typename Column>
concept has_name_token = is_column<Column> && (has_key_name<Column> || has_primary_key_name<Column>);

}  // namespace dreamor::internal

// A column prints as its name token: format("Cap_{}_{}", i, j) names a row the way VarTable
// names its variables. The token is printed as it is; a column takes no format spec.
template <typename T, dreamor::ColumnName Name>
  requires dreamor::internal::has_name_token<dreamor::Col<T, Name>>
struct std::formatter<dreamor::Col<T, Name>, char> {
  constexpr auto parse(std::format_parse_context& context) {
    const auto* it = context.begin();
    if (it != context.end() && *it != '}') { throw std::format_error("A column takes no format spec: write {}."); }
    return it;
  }

  template <typename FormatContext>
  auto format(const dreamor::Col<T, Name>& column, FormatContext& context) const {
    return std::format_to(context.out(), "{}", dreamor::internal::toNameToken(column));
  }
};
