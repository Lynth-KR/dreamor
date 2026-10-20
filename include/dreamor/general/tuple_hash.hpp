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

#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <type_traits>

namespace dreamor::general {

// Element types must be std::hash-able; entity pointers, integers, enums and strings are.
struct TupleHash {
  template <typename... Ts>
  [[nodiscard]] std::size_t operator()(const std::tuple<Ts...>& tuple) const noexcept {
    std::size_t seed = kFnv1aOffsetBasis;
    std::apply(
        [&seed](const auto&... elements) {
          ((seed = mix(seed, std::hash<std::remove_cvref_t<decltype(elements)>>{}(elements))), ...);
        },
        tuple);
    return seed;
  }

 private:
  static constexpr std::uint64_t kFnv1aOffsetBasis = 0xcbf29ce484222325ULL;  // FNV-1a 64-bit basis
  static constexpr std::uint64_t kGoldenRatio64    = 0x9e3779b97f4a7c15ULL;  // 2^64 / phi
  static constexpr std::uint64_t kMixMult1         = 0xbf58476d1ce4e5b9ULL;  // splitmix64 round 1
  static constexpr std::uint64_t kMixMult2         = 0x94d049bb133111ebULL;  // splitmix64 round 2

  // splitmix64's shift distances, tuned together with the multipliers above - don't
  // change one without the other, the sequence is what gives full avalanche.
  static constexpr std::uint32_t kMixShift1 = 30U;
  static constexpr std::uint32_t kMixShift2 = 27U;
  static constexpr std::uint32_t kMixShift3 = 31U;

  // splitmix64's finalizer. Needed because libstdc++'s std::hash is the identity for
  // integers and near-identity for pointers, so the mixer does all the avalanche work.
  //
  // NOT Boost's classic `seed ^ (v + 0x9e3779b9 + (seed<<6) + (seed>>2))` - that's a
  // 32-bit recipe, and its 6/2 shifts leave the high half of a 64-bit word unmixed. On a
  // 1000x1000 grid of integer tuples that collapsed 1e6 keys onto ~68k distinct hashes.
  [[nodiscard]] static std::size_t mix(std::size_t seed, std::size_t value) noexcept {
    auto state = static_cast<std::uint64_t>(seed) + kGoldenRatio64 + static_cast<std::uint64_t>(value);
    state ^= state >> kMixShift1;
    state *= kMixMult1;
    state ^= state >> kMixShift2;
    state *= kMixMult2;
    state ^= state >> kMixShift3;
    return static_cast<std::size_t>(state);
  }
};

}  // namespace dreamor::general
