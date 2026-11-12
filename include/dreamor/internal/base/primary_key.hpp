// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The primary key of an entity or of a user's value type, and the text that joins the parts of a composite key in a
// name. Each is a member function, primaryKey() or primaryKeySeparator(), or else a free function that
// argument-dependent lookup finds, as std::ranges::begin finds begin.
#pragma once

#include <concepts>
#include <string_view>
#include <type_traits>
#include <utility>

namespace dreamor::internal::inline base::primary_key_lookup {

// Hides any primaryKey in an enclosing namespace, so that the call below finds the type's own function by
// argument-dependent lookup alone, as the poison pills of std::ranges::begin do. The function is declared, not
// deleted, because cl.exe does no argument-dependent lookup in a requires-expression after it finds a deleted
// function (VSO-1681199). The MSVC STL declares the poison pill of ranges::begin in the same way there. A call with
// an argument never selects this function, which has no parameter.
void primaryKey();

template <typename T>
concept member_keyed = requires(const T& entity) { entity.primaryKey(); };

template <typename T>
concept free_keyed = requires(const T& entity) { primaryKey(entity); };

struct Fn {
  template <typename T>
    requires(member_keyed<T> || free_keyed<T>)
  [[nodiscard]] constexpr decltype(auto) operator()(const T& entity) const {
    if constexpr (member_keyed<T>) {
      return entity.primaryKey();
    } else {
      return primaryKey(entity);
    }
  }
};

void primaryKeySeparator();  // Declared, not deleted, for the reason given above primaryKey.

template <typename T>
concept member_separated = requires(const T& entity) {
  { entity.primaryKeySeparator() } -> std::convertible_to<std::string_view>;
};

template <typename T>
concept free_separated = requires(const T& entity) {
  { primaryKeySeparator(entity) } -> std::convertible_to<std::string_view>;
};

struct SeparatorFn {
  template <typename T>
  [[nodiscard]] constexpr std::string_view operator()(const T& entity) const {
    if constexpr (member_separated<T>) {
      return entity.primaryKeySeparator();
    } else if constexpr (free_separated<T>) {
      return primaryKeySeparator(entity);
    } else {
      return "_";
    }
  }
};

}  // namespace dreamor::internal::inline base::primary_key_lookup

namespace dreamor::internal::inline base {

// Returns the primary key of an entity or a value.
// NOLINTNEXTLINE(readability-identifier-naming): a function object, named as its function
inline constexpr primary_key_lookup::Fn primaryKeyOf{};

// Returns the text that joins the parts of a composite primary key in a name: primaryKeySeparator() if the type has
// one, and "_" otherwise.
// NOLINTNEXTLINE(readability-identifier-naming): a function object, named as its function
inline constexpr primary_key_lookup::SeparatorFn primaryKeySeparatorOf{};

// A type that has a primary key, by a member primaryKey() or a free primaryKey().
template <typename T>
concept primary_keyed = primary_key_lookup::member_keyed<T> || primary_key_lookup::free_keyed<T>;

// The type of T's primary key, without references and cv-qualifiers.
template <typename T>
using PrimaryKeyOf = std::remove_cvref_t<decltype(primaryKeyOf(std::declval<const T&>()))>;

}  // namespace dreamor::internal::inline base
