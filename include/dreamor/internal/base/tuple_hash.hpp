// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The hash of a tuple key, for which std::hash has none. The set layer and the backends use the same hash, so that
// they hash a key alike.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#if defined(_MSC_VER) && !defined(__SIZEOF_INT128__)
#  include <intrin.h>  // For __umulh in foldedMultiply.
#endif

namespace dreamor::internal::inline base {

// Returns the 128-bit product of a and b with its two halves combined by xor, a folded multiply, in standard C++.
// The product is made from four 32x32 -> 64 products of the halves. The cross terms are summed so that no carry is
// lost: the sum stays below 2^64.
[[nodiscard]] constexpr std::uint64_t foldedMultiplyPortable(std::uint64_t a, std::uint64_t b) noexcept {
  constexpr unsigned kHalfBits = 32U;
  constexpr std::uint64_t kLow = 0xffffffffULL;
  const std::uint64_t a_low    = a & kLow;
  const std::uint64_t a_high   = a >> kHalfBits;
  const std::uint64_t b_low    = b & kLow;
  const std::uint64_t b_high   = b >> kHalfBits;
  const std::uint64_t low_low  = a_low * b_low;
  const std::uint64_t high_low = a_high * b_low;
  const std::uint64_t cross    = (low_low >> kHalfBits) + (high_low & kLow) + (a_low * b_high);
  const std::uint64_t high     = (a_high * b_high) + (high_low >> kHalfBits) + (cross >> kHalfBits);
  return (a * b) ^ high;
}

// Returns the same value as foldedMultiplyPortable, with the compiler's own wide multiply if it has one: unsigned
// __int128 on GCC and Clang, and __umulh, the high half, on MSVC for x64 and ARM64. Other platforms use the halves.
// Every branch returns the same value, bit for bit, so a key hashes the same on every platform.
[[nodiscard]] inline std::uint64_t foldedMultiply(std::uint64_t a, std::uint64_t b) noexcept {
#ifdef __SIZEOF_INT128__
  __extension__ using Wide     = unsigned __int128;  // __int128 is not ISO C++; __extension__ silences -Wpedantic.
  constexpr unsigned kWordBits = 64U;
  const Wide product           = static_cast<Wide>(a) * b;
  return static_cast<std::uint64_t>(product) ^ static_cast<std::uint64_t>(product >> kWordBits);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
  return (a * b) ^ __umulh(a, b);
#else
  return foldedMultiplyPortable(a, b);
#endif
}

// Returns the hash of one element before TupleHash mixes it in, in 64 bits on every platform. An integer, an enum or
// a pointer is hashed as its bits, and any other value by std::hash. std::hash is not used for every element,
// because each standard library has its own; the MSVC STL, for example, runs FNV-1a over the bytes. The bits give
// the same hash and the same code with every library. The overload for a column is next to the column, in
// internal/index_set/column.hpp, and TupleHash finds it by argument-dependent lookup.
template <typename T>
[[nodiscard]] constexpr std::uint64_t hashElement(const T& element) noexcept;

// The start value of every hash here, FNV-1a's 64-bit offset basis, and the odd multiplier, 2^64 / phi.
inline constexpr std::uint64_t kHashSeed       = 0xcbf29ce484222325ULL;
inline constexpr std::uint64_t kHashMultiplier = 0x9e3779b97f4a7c15ULL;

template <typename T>
[[nodiscard]] constexpr std::uint64_t hashElement(const T& element) noexcept {
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
    return static_cast<std::uint64_t>(element);
  } else if constexpr (std::is_pointer_v<T>) {
    return static_cast<std::uint64_t>(std::bit_cast<std::uintptr_t>(element));
  } else {
    return std::hash<T>{}(element);
  }
}

// The elements of a key are integers, enums, pointers, Refs, columns of any of these, or values that std::hash
// accepts, such as a string or a user's value type with its own std::hash. The state has 64 bits on every
// platform, so integer keys hash the same everywhere. If std::size_t has 32 bits, as on wasm32, the hash is the low
// half of the state.
struct TupleHash {
  template <typename... Ts>
  [[nodiscard]] std::size_t operator()(const std::tuple<Ts...>& tuple) const noexcept {
    std::uint64_t state = kSeed;
    std::apply([&state](const auto&... elements) { ((state = mix(state, hashElement(elements))), ...); }, tuple);
    return static_cast<std::size_t>(state);
  }

 private:
  static constexpr std::uint64_t kSeed       = kHashSeed;
  static constexpr std::uint64_t kMultiplier = kHashMultiplier;

  // Returns the folded multiply of (state + value) and an odd constant. Its high half carries every input bit into
  // the low bits that a table indexes by. That is all the mixing that integers and pointers need when they are
  // handed over as their bits.
  [[nodiscard]] static std::uint64_t mix(std::uint64_t state, std::uint64_t value) noexcept {
    return foldedMultiply(state + value, kMultiplier);
  }
};

}  // namespace dreamor::internal::inline base
