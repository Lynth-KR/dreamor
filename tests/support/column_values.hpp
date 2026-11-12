// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The values a random test's columns take: index i as a value of each column type the differential tests use - an
// integer (a signed one alternating around zero: 0, -1, 1, -2, ...), an enum of that integer, a Ref to an entity of a
// frozen EntityVector, a text too long to be held inside a string (viewed or owned), a Code of the user's whose hash
// reads half of it - and back. A test draws its data as indices, checks against a reference over indices, and turns
// dreamor's rows back into indices to compare them.
#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "dreamor/entity_vector.hpp"
#include "dreamor/index_set.hpp"

namespace column_values {

inline constexpr std::size_t kValuePool  = 4096;  // indices 0..kValuePool have a value of every column type
inline constexpr std::size_t kTextPrefix = 24;    // a text is this many v's, then its index

// The values of an entity column: Refs to the entities of a frozen EntityVector, each knowing its index.
struct Entity {
  std::size_t id;
};

inline std::size_t primaryKey(const Entity& entity) {
  return entity.id;
}

// A value type of the user's for a value column: == and a std::hash, specialized below before any column of it is
// declared. The hash reads the family alone, so the four members of a family collide in every index. Its primary key
// names it in a variable's name.
struct Code {
  std::uint32_t family;
  std::uint16_t member;

  [[nodiscard]] bool operator==(const Code&) const = default;
};

inline std::tuple<std::uint32_t, std::uint16_t> primaryKey(const Code& code) {
  return {code.family, code.member};
}

inline const dreamor::EntityVector<Entity>& entities() {
  static const dreamor::EntityVector<Entity> all = [] {
    dreamor::EntityVector<Entity> out;
    for (std::size_t i = 0; i <= kValuePool; ++i) {
      static_cast<void>(out.emplace(Entity{i}));
    }
    out.freeze();
    return out;
  }();
  return all;
}

// The texts a text column views or copies: longer than a short string holds inline, so each is apart in memory.
inline const std::vector<std::string>& texts() {
  static const std::vector<std::string> all = [] {
    std::vector<std::string> out(kValuePool + 1);
    for (std::size_t i = 0; i < out.size(); ++i) {
      out[i] = std::string(kTextPrefix, 'v') + std::to_string(i);
    }
    return out;
  }();
  return all;
}

template <typename T>
inline constexpr bool kText = std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>;

// A column's value for index i, and back.
template <typename T>
[[nodiscard]] T valueAt(std::size_t i) {
  if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
    const auto half = static_cast<std::ptrdiff_t>((i + 1) / 2);
    return static_cast<T>(i % 2 == 0 ? half : -half);
  } else if constexpr (std::is_enum_v<T> || std::is_integral_v<T>) {
    return static_cast<T>(i);
  } else if constexpr (dreamor::internal::ref<T>) {
    static_assert(std::is_same_v<T, dreamor::Ref<Entity>>, "an entity column holds a Ref<Entity>");
    return entities().at(i);
  } else if constexpr (std::is_same_v<T, Code>) {
    return Code{.family = static_cast<std::uint32_t>(i / 4), .member = static_cast<std::uint16_t>(i % 4)};
  } else {
    static_assert(kText<T>, "a column holds an integer, an enum, a Ref<Entity>, a Code or a text");
    return T{texts()[i]};
  }
}

template <typename Column>
[[nodiscard]] Column columnAt(std::size_t i) {
  using Value = Column::value_type;
  return Column{valueAt<Value>(i)};
}

template <typename Value>
[[nodiscard]] std::size_t indexOfValue(const Value& value);

template <typename Column>
[[nodiscard]] std::size_t indexOf(const Column& column) {
  return indexOfValue(column.get());
}

template <typename Value>
[[nodiscard]] std::size_t indexOfValue(const Value& value) {
  if constexpr (std::is_enum_v<Value>) {
    return static_cast<std::size_t>(static_cast<std::underlying_type_t<Value>>(value));
  } else if constexpr (std::is_integral_v<Value> && std::is_signed_v<Value>) {
    const auto v = static_cast<std::int64_t>(value);
    return v >= 0 ? 2 * static_cast<std::size_t>(v) : (2 * static_cast<std::size_t>(-v)) - 1;
  } else if constexpr (std::is_integral_v<Value>) {
    return static_cast<std::size_t>(value);
  } else if constexpr (dreamor::internal::ref<Value>) {
    return value->id;
  } else if constexpr (std::is_same_v<Value, Code>) {
    return (std::size_t{value.family} * 4) + value.member;
  } else {
    const std::string_view text{value};
    std::size_t i = texts().size();  // a text of no index reads as none of them
    const std::from_chars_result read =
        std::from_chars(text.data() + std::min(kTextPrefix, text.size()), text.data() + text.size(), i);
    static_cast<void>(read);
    return i;
  }
}

// The most values a case may give a column of T, at most kValuePool: index `values` must still be a value of T.
template <typename T>
[[nodiscard]] consteval std::size_t mostValuesOf() {
  if constexpr (std::is_enum_v<T>) {
    return std::min(kValuePool, static_cast<std::size_t>(std::numeric_limits<std::underlying_type_t<T>>::max()));
  } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
    return std::min(kValuePool, 2 * static_cast<std::size_t>(std::numeric_limits<T>::max()));
  } else if constexpr (std::is_integral_v<T>) {
    return std::min(kValuePool, static_cast<std::size_t>(std::numeric_limits<T>::max()));
  } else {
    return kValuePool;
  }
}

template <typename Column>
[[nodiscard]] auto valuesOf(const std::vector<std::size_t>& indices) {
  using Value = Column::value_type;
  std::vector<Value> out;
  out.reserve(indices.size());
  for (const std::size_t i : indices) {
    out.push_back(valueAt<Value>(i));
  }
  return out;
}

}  // namespace column_values

template <>
struct std::hash<column_values::Code> {
  [[nodiscard]] std::size_t operator()(const column_values::Code& code) const noexcept {
    return std::hash<std::uint32_t>{}(code.family);
  }
};
