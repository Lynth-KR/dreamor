// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A replacement of the global operator new that can be made to fail: at the N-th allocation from a point on, for a
// test that makes every allocation of a run fail in turn, or at the next allocation of a given size, for a test of what
// a container keeps when one allocation throws. A program may replace operator new in one translation unit only, so a
// test program includes this header once, and only a test that is a program of its own includes it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "dreamor/internal/base/inline.hpp"

namespace failing_allocation {

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): the test arms them before the allocations
// The number of allocations since arm(), and the one of them that throws std::bad_alloc, or 0 for none.
inline std::size_t g_counted = 0;
inline std::size_t g_fail_at = 0;
inline bool g_counting       = false;
inline bool g_fired          = false;
// The size of the next allocation that throws std::bad_alloc, or 0 for none.
inline std::size_t g_fail_size = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Counts the allocations from now on, and makes the n-th of them throw std::bad_alloc; n = 0 only counts. One
// allocation fails, and the allocations after it succeed and are not counted.
inline void arm(std::size_t n) noexcept {
  g_counted  = 0;
  g_fail_at  = n;
  g_counting = true;
  g_fired    = false;
}

// Stops counting, and returns the number of allocations counted since arm().
inline std::size_t disarm() noexcept {
  g_counting = false;
  g_fail_at  = 0;
  return g_counted;
}

// True if the armed allocation has thrown since the last arm().
[[nodiscard]] inline bool fired() noexcept {
  return g_fired;
}

// Makes the next allocation of `size` bytes throw std::bad_alloc. One allocation of that size fails, and the
// allocations after it succeed.
inline void failNextOfSize(std::size_t size) noexcept {
  g_fail_size = size;
}

// Takes one allocation of `size` bytes: throws if it is the one of the armed size, or the armed one of the count. Each
// is disarmed before its throw, so that what runs while the exception propagates allocates freely.
inline void count(std::size_t size) {
  if (size != 0 && size == g_fail_size) {
    g_fail_size = 0;
    throw std::bad_alloc{};
  }
  if (!g_counting) { return; }
  ++g_counted;
  if (g_counted == g_fail_at) {
    g_counting = false;
    g_fail_at  = 0;
    g_fired    = true;
    throw std::bad_alloc{};
  }
}

// An over-aligned block is taken from malloc with room to align it, and the address malloc returned is kept just below
// the aligned block, for the delete to free. std::aligned_alloc would do as well, but the MSVC CRT has none.
// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,*-reinterpret-cast): the replacements' memory
[[nodiscard]] inline void* alignedBlock(std::size_t size, std::size_t align) {
  const std::size_t room = size + align + sizeof(void*);
  if (room < size) { throw std::bad_alloc{}; }
  void* const raw = std::malloc(room);
  if (raw == nullptr) { throw std::bad_alloc{}; }
  const std::uintptr_t start   = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
  const std::uintptr_t aligned = (start + align - 1) & ~(std::uintptr_t{align} - 1);
  void** const block           = reinterpret_cast<void**>(aligned);  // NOLINT(performance-no-int-to-ptr): aligned
  block[-1]                    = raw;
  return static_cast<void*>(block);
}

inline void freeAlignedBlock(void* block) noexcept {
  if (block == nullptr) { return; }
  std::free(static_cast<void**>(block)[-1]);
}

// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,*-reinterpret-cast)

}  // namespace failing_allocation

// The replacements are defined in a header, which each test program that uses them includes once. Every form is
// replaced, the array and the nothrow ones too: a sanitizer's runtime defines its own of each, which would neither be
// counted nor match these deletes.
// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,misc-definitions-in-headers): replacements
void* operator new(std::size_t size) {
  failing_allocation::count(size);
  if (void* memory = std::malloc(size == 0 ? 1 : size)) { return memory; }
  throw std::bad_alloc{};
}

void* operator new[](std::size_t size) {
  return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t align) {
  failing_allocation::count(size);
  return failing_allocation::alignedBlock(size == 0 ? 1 : size, static_cast<std::size_t>(align));
}

void* operator new[](std::size_t size, std::align_val_t align) {
  return ::operator new(size, align);
}

void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
  try {
    return ::operator new(size);
  } catch (const std::bad_alloc&) { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
  try {
    return ::operator new(size);
  } catch (const std::bad_alloc&) { return nullptr; }
}

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t& /*tag*/) noexcept {
  try {
    return ::operator new(size, align);
  } catch (const std::bad_alloc&) { return nullptr; }
}

void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t& /*tag*/) noexcept {
  try {
    return ::operator new(size, align);
  } catch (const std::bad_alloc&) { return nullptr; }
}

// The deletes are not inlined: GCC at -O3 would otherwise see free() of what a new-expression allocated at the call
// site, and warn (-Wmismatched-new-delete), though this new and these deletes are a pair. The sized deletes exist
// only where sized deallocation is on, as GCC has it and Clang 18 does not by default: GCC warns if they are missing
// (-Wsized-deallocation), and Clang 18 if they have no declaration (-Wmissing-prototypes).
DREAMOR_NOINLINE void operator delete(void* memory) noexcept {
  std::free(memory);
}

DREAMOR_NOINLINE void operator delete[](void* memory) noexcept {
  std::free(memory);
}

DREAMOR_NOINLINE void operator delete(void* memory, std::align_val_t /*align*/) noexcept {
  failing_allocation::freeAlignedBlock(memory);
}

DREAMOR_NOINLINE void operator delete[](void* memory, std::align_val_t /*align*/) noexcept {
  failing_allocation::freeAlignedBlock(memory);
}

DREAMOR_NOINLINE void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept {
  std::free(memory);
}

DREAMOR_NOINLINE void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept {
  std::free(memory);
}

DREAMOR_NOINLINE void operator delete(void* memory,
                                      std::align_val_t /*align*/,
                                      const std::nothrow_t& /*tag*/) noexcept {
  failing_allocation::freeAlignedBlock(memory);
}

DREAMOR_NOINLINE void operator delete[](void* memory,
                                        std::align_val_t /*align*/,
                                        const std::nothrow_t& /*tag*/) noexcept {
  failing_allocation::freeAlignedBlock(memory);
}
#ifdef __cpp_sized_deallocation
DREAMOR_NOINLINE void operator delete(void* memory, std::size_t /*size*/) noexcept {
  std::free(memory);
}

DREAMOR_NOINLINE void operator delete[](void* memory, std::size_t /*size*/) noexcept {
  std::free(memory);
}

DREAMOR_NOINLINE void operator delete(void* memory, std::size_t /*size*/, std::align_val_t /*align*/) noexcept {
  failing_allocation::freeAlignedBlock(memory);
}

DREAMOR_NOINLINE void operator delete[](void* memory, std::size_t /*size*/, std::align_val_t /*align*/) noexcept {
  failing_allocation::freeAlignedBlock(memory);
}
#endif
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory,misc-definitions-in-headers)
