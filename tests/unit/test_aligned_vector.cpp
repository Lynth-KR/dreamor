// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// internal::AlignedVector on its own, and the sets every derivation makes: their rows start on a
// cache line's boundary.
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/index_set.hpp"
#include "dreamor/internal/base/aligned_vector.hpp"
#include "support/check.hpp"

namespace {
DREAMOR_VALUE_COLUMN(cI, std::size_t);
DREAMOR_VALUE_COLUMN(cJ, std::size_t);
}  // namespace

namespace {

bool onBoundary(const void* at, std::size_t align) {
  return std::bit_cast<std::uintptr_t>(at) % align == 0;
}

template <typename Set>
bool rowsOnLine(const Set& set) {
  return set.size() == 0 || onBoundary(&*set.begin(), 64);
}

}  // namespace

int main() try {
  // every block a vector grows into, and one aligned past what new gives on its own
  dreamor::internal::AlignedVector64<char> bytes;
  bool grown = true;
  for (std::size_t k = 0; k < 100000; ++k) {
    bytes.push_back(static_cast<char>(k));
    grown = grown && onBoundary(bytes.data(), 64);
  }
  DREAMOR_CHECK(grown && bytes.size() == 100000 && bytes[99999] == static_cast<char>(99999));
  dreamor::internal::AlignedVector<std::uint64_t, 4096> paged(10, 7);
  DREAMOR_CHECK(onBoundary(paged.data(), 4096) && paged[9] == 7);

  // a move hands the block over; a copy has one of its own, aligned too
  const char* block = bytes.data();
  dreamor::internal::AlignedVector64<char> moved{std::move(bytes)};
  DREAMOR_CHECK(moved.data() == block);
  dreamor::internal::AlignedVector64<char> copy{moved};
  copy.push_back('x');
  DREAMOR_CHECK(copy.data() != block && onBoundary(copy.data(), 64) && copy.size() == moved.size() + 1);

  // the allocator rebinds as containers ask it to
  using Line    = std::integral_constant<std::size_t, dreamor::internal::kCacheLine>;
  using Rebound = std::allocator_traits<dreamor::internal::AlignedAllocator<char, Line>>::rebind_alloc<long double>;
  static_assert(std::is_same_v<Rebound, dreamor::internal::AlignedAllocator<long double, Line>>);
  static_assert(std::allocator_traits<Rebound>::is_always_equal::value);
  const dreamor::internal::AlignedAllocator<char, Line> chars;
  const Rebound wide{chars};  // from the allocator of another element type, as a container converts it
  DREAMOR_CHECK(chars == chars && wide == Rebound{});

  // sets: collected, filtered, joined, projected, combined, and empty
  const std::vector<std::size_t> I{1, 2, 3, 4, 5};
  const std::vector<std::size_t> J{10, 20, 30};
  const auto IJ = dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).build();
  const auto kept =
      dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).where([](cI i) { return i.get() % 2 == 1; }).build();
  const auto Is     = dreamor::index_set::relational::from<cI>(I).build();
  const auto Js     = dreamor::index_set::relational::from<cJ>(J).build();
  const auto onI    = dreamor::index_set::relational::from(Is).join(IJ).build();
  const auto picked = IJ.select<cI>();
  DREAMOR_CHECK(rowsOnLine(IJ) && rowsOnLine(kept) && rowsOnLine(Is) && rowsOnLine(Js) && rowsOnLine(onI));
  DREAMOR_CHECK(rowsOnLine(picked) && picked.size() == I.size());
  const auto both    = dreamor::unite(IJ, kept);
  const auto common  = dreamor::intersect(IJ, kept);
  const auto missing = dreamor::difference(IJ, kept);
  DREAMOR_CHECK(rowsOnLine(both) && rowsOnLine(common) && rowsOnLine(missing));
  DREAMOR_CHECK(both.size() == IJ.size() && common.size() == kept.size() && missing.size() == IJ.size() - kept.size());
  const std::vector<std::size_t> nothing;
  const auto none = dreamor::index_set::relational::from<cI>(nothing).cross<cJ>(nothing).build();
  DREAMOR_CHECK(rowsOnLine(none) && none.empty());
  return report("aligned_vector");
} catch (...) { return ::uncaught(); }
