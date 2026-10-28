// SPDX-License-Identifier: Apache-2.0
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

namespace dreamor::general {

// One element's hash before TupleHash mixes it in. An integer, an enum or a pointer goes in as its
// bits - what libstdc++'s std::hash returns for it - and anything else through std::hash. The
// standard leaves std::hash to each library: the MSVC STL runs FNV-1a over the bytes, eight
// multiplies for a pointer, before TupleHash mixes again. Taking the bits makes the hash, and the
// code a key's lookup compiles to, the same on every standard library.
template <typename T>
[[nodiscard]] constexpr std::size_t hashElement(const T& element) noexcept {
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
    return static_cast<std::size_t>(element);
  } else if constexpr (std::is_pointer_v<T>) {
    return static_cast<std::size_t>(std::bit_cast<std::uintptr_t>(element));
  } else {
    return std::hash<T>{}(element);
  }
}

// Element types are integers, enums and pointers, or anything std::hash takes (a string, a column).
struct TupleHash {
  template <typename... Ts>
  [[nodiscard]] std::size_t operator()(const std::tuple<Ts...>& tuple) const noexcept {
    std::size_t seed = kSeed;
    std::apply([&seed](const auto&... elements) { ((seed = mix(seed, hashElement(elements))), ...); }, tuple);
    return seed;
  }

 private:
  static constexpr std::uint64_t kSeed       = 0xcbf29ce484222325ULL;  // FNV-1a's 64-bit offset basis
  static constexpr std::uint64_t kMultiplier = 0x9e3779b97f4a7c15ULL;  // 2^64 / phi, odd
  static constexpr unsigned kWordBits        = 64U;

  // splitmix64's finalizer, where there are no 128-bit integers: its multipliers and shifts
  // were tuned together, so one does not change without the others.
  static constexpr std::uint64_t kSplitMult1 = 0xbf58476d1ce4e5b9ULL;
  static constexpr std::uint64_t kSplitMult2 = 0x94d049bb133111ebULL;
  static constexpr unsigned kSplitShift1     = 30U;
  static constexpr unsigned kSplitShift2     = 27U;
  static constexpr unsigned kSplitShift3     = 31U;

  // Folds one element into the state: the 128-bit product of (state + value) and an odd
  // constant, its two halves xored - the folded multiply Abseil's hash and foldhash mix with.
  // The high half carries every input bit into the low bits a table indexes by, which is all
  // the mixing there is to do: hashElement hands integers and pointers over as their bits. One
  // multiply per element, so a 3-column key hashes in about two
  // thirds of the time splitmix64 per element took. Without 128-bit integers, splitmix64 below.
  [[nodiscard]] static std::size_t mix(std::size_t seed, std::size_t value) noexcept {
    const auto state = static_cast<std::uint64_t>(seed) + static_cast<std::uint64_t>(value);
#ifdef __SIZEOF_INT128__
    __extension__ using Wide = unsigned __int128;  // __extension__: not ISO C++, so not a -Wpedantic warning
    const Wide product       = static_cast<Wide>(state) * kMultiplier;
    return static_cast<std::size_t>(static_cast<std::uint64_t>(product) ^
                                    static_cast<std::uint64_t>(product >> kWordBits));
#else
    std::uint64_t z = state + kMultiplier;
    z               = (z ^ (z >> kSplitShift1)) * kSplitMult1;
    z               = (z ^ (z >> kSplitShift2)) * kSplitMult2;
    return static_cast<std::size_t>(z ^ (z >> kSplitShift3));
#endif
  }
};

}  // namespace dreamor::general
