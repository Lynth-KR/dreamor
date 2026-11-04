// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// File: tuple_hash.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// std::hash has no specialization for std::tuple, so anything keyed by a tuple needs its
// own. Shared by the index-set layer and the variable layer so both hash identically.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>
#if defined(_MSC_VER) && !defined(__SIZEOF_INT128__)
#  include <intrin.h>  // __umulh, for foldedMultiply
#endif

namespace dreamor::general {

// The 128-bit product of a and b, its two halves xored: the folded multiply Abseil's hash and foldhash mix
// with. This one is standard C++ on every platform: four 32x32 -> 64 products of the halves, the cross terms
// summed so that no carry is lost (the sum stays below 2^64).
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

// The same value through the compiler's own wide multiply where it has one - GCC's and Clang's unsigned
// __int128, MSVC's __umulh (the high half, on x64 and ARM64) - and through the halves elsewhere. Every
// branch gives the same value, bit for bit, so a key hashes the same on every platform.
[[nodiscard]] inline std::uint64_t foldedMultiply(std::uint64_t a, std::uint64_t b) noexcept {
#ifdef __SIZEOF_INT128__
  __extension__ using Wide     = unsigned __int128;  // __extension__: not ISO C++, so not a -Wpedantic warning
  constexpr unsigned kWordBits = 64U;
  const Wide product           = static_cast<Wide>(a) * b;
  return static_cast<std::uint64_t>(product) ^ static_cast<std::uint64_t>(product >> kWordBits);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
  return (a * b) ^ __umulh(a, b);
#else
  return foldedMultiplyPortable(a, b);
#endif
}

// One element's hash before TupleHash mixes it in, in 64 bits on every platform. An integer, an enum or
// a pointer goes in as its bits - what libstdc++'s std::hash returns for it - and anything else through
// std::hash. The standard leaves std::hash to each library: the MSVC STL runs FNV-1a over the bytes,
// eight multiplies for a pointer, before TupleHash mixes again. Taking the bits makes the hash, and the
// code a key's lookup compiles to, the same on every standard library. A column is unwrapped by the
// overload beside it (index_set/column.hpp), which TupleHash finds by argument-dependent lookup.
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

// Element types are integers, enums and pointers, columns of them, or anything std::hash takes (a
// string). The state is 64 bits on every platform, so integer keys hash the same everywhere; where
// std::size_t is 32 bits (wasm32), the hash is the state's low half.
struct TupleHash {
  template <typename... Ts>
  [[nodiscard]] std::size_t operator()(const std::tuple<Ts...>& tuple) const noexcept {
    std::uint64_t state = kSeed;
    std::apply([&state](const auto&... elements) { ((state = mix(state, hashElement(elements))), ...); }, tuple);
    return static_cast<std::size_t>(state);
  }

 private:
  static constexpr std::uint64_t kSeed       = 0xcbf29ce484222325ULL;  // FNV-1a's 64-bit offset basis
  static constexpr std::uint64_t kMultiplier = 0x9e3779b97f4a7c15ULL;  // 2^64 / phi, odd

  // Folds one element into the state: the folded multiply of (state + value) and an odd constant. The
  // high half carries every input bit into the low bits a table indexes by, which is all the mixing
  // there is to do: hashElement hands integers and pointers over as their bits. One multiply per
  // element.
  [[nodiscard]] static std::uint64_t mix(std::uint64_t state, std::uint64_t value) noexcept {
    return foldedMultiply(state + value, kMultiplier);
  }
};

}  // namespace dreamor::general
