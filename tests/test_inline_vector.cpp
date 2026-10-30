// SPDX-License-Identifier: Apache-2.0
// internal::InlineVector on its own: elements inside the object up to N, on the heap past them, and
// copies and moves either way, each vector owning its storage.
#include <cstddef>
#include <numeric>
#include <span>
#include <utility>

#include "check.hpp"
#include "dreamor/internal/inline_vector.hpp"

using Small = dreamor::internal::InlineVector<int, 4>;

namespace {

bool holds(const Small& v, std::size_t count, int first) {
  bool same = v.size() == count;
  for (std::size_t k = 0; same && k < count; ++k) {
    same = v[k] == first + static_cast<int>(k);
  }
  return same;
}

Small filled(std::size_t count, int first) {
  Small v;
  for (std::size_t k = 0; k < count; ++k) {
    v.pushBack(first + static_cast<int>(k));
  }
  return v;
}

}  // namespace

int main() try {
  // inside the object up to N, the same elements on the heap past it
  Small inside              = filled(4, 0);
  const int* inline_storage = inside.data();
  DREAMOR_CHECK(holds(inside, 4, 0) && !inside.empty());
  inside.pushBack(4);
  DREAMOR_CHECK(holds(inside, 5, 0) && inside.data() != inline_storage);
  const Small large = filled(100, 7);
  DREAMOR_CHECK(holds(large, 100, 7) && std::accumulate(large.begin(), large.end(), 0) == (7 * 100) + (99 * 100 / 2));

  // copies own their storage
  const Small small = filled(3, 1);
  Small copy        = small;
  copy[0]           = 42;
  DREAMOR_CHECK(holds(small, 3, 1) && copy[0] == 42 && copy.data() != small.data());
  Small largeCopy = large;
  DREAMOR_CHECK(holds(largeCopy, 100, 7) && largeCopy.data() != large.data());
  largeCopy = small;  // keeps its heap storage, holds the small one's elements
  DREAMOR_CHECK(holds(largeCopy, 3, 1));
  largeCopy.pushBack(4);
  DREAMOR_CHECK(holds(largeCopy, 4, 1));
  Small inlineCopy = filled(2, 0);
  inlineCopy       = large;  // inline elements, then the heap
  DREAMOR_CHECK(holds(inlineCopy, 100, 7));
  const Small& same = copy;
  copy              = same;  // assigning a vector to itself
  DREAMOR_CHECK(copy.size() == 3 && copy[0] == 42);

  // a move takes heap storage as it is, and copies inline elements into its own
  Small source         = filled(50, 3);
  const int* heap_data = source.data();
  const Small moved{std::move(source)};
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): what a move leaves is the case
  DREAMOR_CHECK(holds(moved, 50, 3) && moved.data() == heap_data && source.empty());
  Small inlineSource = filled(2, 9);
  Small movedInline{std::move(inlineSource)};
  DREAMOR_CHECK(holds(movedInline, 2, 9));
  movedInline = filled(60, 0);  // a move onto a vector that held elements inline
  DREAMOR_CHECK(holds(movedInline, 60, 0));
  movedInline = filled(1, 5);  // and from the heap back inside
  DREAMOR_CHECK(holds(movedInline, 1, 5));
  movedInline.pushBack(6);
  DREAMOR_CHECK(holds(movedInline, 2, 5));

  // reserve, and a span over the elements
  Small reserved;
  reserved.reserve(10);
  reserved.pushBack(1);
  const std::span<const int> view = reserved;
  DREAMOR_CHECK(view.size() == 1 && view[0] == 1);
  return report("inline_vector");
} catch (...) { return ::uncaught(); }
