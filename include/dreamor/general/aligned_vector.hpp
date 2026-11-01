// SPDX-License-Identifier: Apache-2.0
// File: general/aligned_vector.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// A std::vector whose elements start on an Align-byte boundary. malloc aligns to 16 bytes only: a
// large block comes from mmap 16 bytes past a page's start, a heap block wherever the heap has room,
// so where a vector's first element falls within a cache line depends on what was allocated and
// freed before. Elements of a size that divides the line - 32 or 64 bytes - then lie within one
// line each, or every one of them across two, by that history alone. Aligned, they lie within one;
// elements of other sizes cross lines as often either way.
#pragma once

#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

namespace dreamor::general {

// Stateless: every instance allocates alike, so a vector moves by handing its block over. The
// alignment comes as a type, std::integral_constant, so that std::allocator_traits rebinds the
// allocator to another element on its own, as it does for templates of types alone.
template <typename T, typename Align>
struct AlignedAllocator {
  static constexpr std::size_t kAlign = Align::value;
  static_assert(kAlign >= alignof(T) && (kAlign & (kAlign - 1)) == 0,
                "AlignedAllocator: the alignment is a power of two, at least the element's own.");

  using value_type = T;

  AlignedAllocator() noexcept = default;
  template <typename U>
  explicit(false) AlignedAllocator(const AlignedAllocator<U, Align>& /*other*/) noexcept {}

  [[nodiscard]] T* allocate(std::size_t n) {
    if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) { throw std::bad_array_new_length{}; }
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t{kAlign}));
  }
  // Unsized: Clang before 19 declares the sized aligned delete only under -fsized-deallocation.
  void deallocate(T* p, std::size_t /*n*/) noexcept { ::operator delete(p, std::align_val_t{kAlign}); }

  [[nodiscard]] friend bool operator==(const AlignedAllocator& /*a*/, const AlignedAllocator& /*b*/) noexcept {
    return true;
  }
};

template <typename T, std::size_t Align>
using AlignedVector = std::vector<T, AlignedAllocator<T, std::integral_constant<std::size_t, Align>>>;

// A cache line on x86-64 and on most ARM cores (Apple's are 128 bytes). A number, not
// std::hardware_destructive_interference_size: GCC warns where a header uses that, as its value
// moves with -mtune and so with the ABI.
inline constexpr std::size_t kCacheLine = 64;

template <typename T>
using AlignedVector64 = AlignedVector<T, kCacheLine>;

}  // namespace dreamor::general
