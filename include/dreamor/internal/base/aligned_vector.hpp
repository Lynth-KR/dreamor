// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A std::vector whose elements start on an Align-byte boundary. malloc aligns to 16 bytes, so the position of the
// first element in a cache line depends on what was allocated before. Elements of 32 or 64 bytes then lie either
// each in one cache line, or each across two. If the vector is aligned, each element lies in one cache line.
#pragma once

#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

namespace dreamor::internal::inline base {

// The allocator has no state, so a vector moves by handing its block of memory over. The alignment is a type, a
// std::integral_constant, because std::allocator_traits can rebind by itself only a template whose parameters are
// all types.
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

  // The unsized delete, because Clang before 19 declares the sized aligned delete only under -fsized-deallocation.
  void deallocate(T* p, std::size_t /*n*/) noexcept { ::operator delete(p, std::align_val_t{kAlign}); }

  [[nodiscard]] friend bool operator==(const AlignedAllocator& /*a*/, const AlignedAllocator& /*b*/) noexcept {
    return true;
  }
};

template <typename T, std::size_t Align>
using AlignedVector = std::vector<T, AlignedAllocator<T, std::integral_constant<std::size_t, Align>>>;

// The size of a cache line on x86-64 and on most ARM cores. Apple's cores have 128 bytes. The constant is not
// std::hardware_destructive_interference_size: GCC warns if a header uses that, because its value changes with
// -mtune and so with the ABI.
inline constexpr std::size_t kCacheLine = 64;

template <typename T>
using AlignedVector64 = AlignedVector<T, kCacheLine>;

}  // namespace dreamor::internal::inline base
