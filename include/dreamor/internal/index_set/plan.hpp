// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The visit order of the sources, and the bookkeeping that lets generation bind columns in that order instead of
// the declared one.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <utility>

#include "dreamor/internal/index_set/binding.hpp"
#include "dreamor/internal/index_set/element.hpp"

namespace dreamor::internal::inline index_set {

// Planning keeps a set's columns as the bits of one std::uint64_t, so a set has at most 64 columns.
inline constexpr std::size_t kMaxColumns = 64;
static_assert(kMaxColumns <= std::numeric_limits<std::uint64_t>::digits);

// Generation binds columns in the visit order, which need not be the order of the element tuple. So the bound values
// are carried with an index_sequence that names the element-tuple column of each value. The helpers below map an
// element-tuple column to its position in that pack.

template <typename Seq>
[[nodiscard]] consteval std::uint64_t maskOfSequence() {
  return []<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
    return ((std::uint64_t{1} << Is) | ... | std::uint64_t{0});
  }(Seq{});
}

template <std::size_t Column, typename Seq>
[[nodiscard]] consteval std::size_t positionInSequence() {
  return []<std::size_t... Is>(std::index_sequence<Is...> /*unused*/) {
    std::size_t position = 0;
    std::size_t found    = 0;
    (((Is == Column ? (found = position) : position), ++position), ...);
    return found;
  }(Seq{});
}

template <typename Columns, typename Bound>
struct RemapToBound;

template <std::size_t... Columns, typename Bound>
struct RemapToBound<std::index_sequence<Columns...>, Bound> {
  using type = std::index_sequence<positionInSequence<Columns, Bound>()...>;
};

template <typename Seq, std::size_t Column>
struct AppendColumn;

template <std::size_t... Is, std::size_t Column>
struct AppendColumn<std::index_sequence<Is...>, Column> {
  using type = std::index_sequence<Is..., Column>;
};

// The visit order is chosen so that predicates run as early as possible. The choice is greedy, and it uses only the
// columns that each source and each predicate has, not how many rows a source holds. At each place in the order:
//
//   1. A source is eligible if every column that it depends on is bound.
//   2. The eligible source that lets the most predicates run at once is preferred.
//   3. On a tie, the source after which some predicate misses the fewest columns is preferred.
//   4. On a tie, a source that depends on bound columns, such as a lateral source, is preferred over a source that
//      does not. The list that a lateral source returns for one row, such as a machine's items, is usually much
//      shorter than a whole container. For the same reason, a one-to-many join is preferred to a many-to-many join
//      when there are no statistics.
//   5. On a tie, the source declared first is chosen.
//
// The visit order decides which column varies fastest, so it decides the order of the result's rows. Set::kVisitOrder
// holds the order that was chosen. The function is constexpr, not consteval, so that bench/plan/ can run the same
// planning on masks that it makes at run time.

template <std::size_t SourceCount, std::size_t PredCount>
// NOLINTNEXTLINE(readability-function-cognitive-complexity): one fixpoint loop; split, it no longer reads as one
[[nodiscard]] constexpr std::array<std::size_t, SourceCount> planVisitOrder(
    std::array<std::uint64_t, SourceCount> produces,
    std::array<std::uint64_t, SourceCount> depends,
    std::array<std::uint64_t, PredCount> predicates) {
  std::array<std::size_t, SourceCount> order{};
  std::array<bool, SourceCount> placed{};
  std::uint64_t bound = 0;

  for (std::size_t step = 0; step < SourceCount; ++step) {
    std::size_t best           = SourceCount;
    std::size_t best_activated = 0;
    std::size_t best_gap       = 0;

    for (std::size_t source = 0; source < SourceCount; ++source) {
      if (placed[source] || (depends[source] & ~bound) != 0) { continue; }
      const std::uint64_t next = bound | produces[source];

      std::size_t activated = 0;
      std::size_t gap       = ~std::size_t{0};
      for (std::size_t p = 0; p < PredCount; ++p) {
        const std::uint64_t missing_now = predicates[p] & ~next;
        if (missing_now == 0) {
          // The predicate ran before, or it runs on this source. Only a predicate that runs on this source counts.
          if ((predicates[p] & ~bound) != 0) { ++activated; }
        } else {
          gap = std::min(gap, static_cast<std::size_t>(std::popcount(missing_now)));
        }
      }
      if (gap == ~std::size_t{0}) {
        gap = 0;  // No predicate still waits for a column.
      }

      // best is tested first because, until a source is picked, best is SourceCount, which is past the end of depends.
      const bool better = (best == SourceCount) || (activated > best_activated) ||
                          (activated == best_activated &&
                           (gap < best_gap || (gap == best_gap && depends[source] != 0 && depends[best] == 0)));
      if (better) {
        best           = source;
        best_activated = activated;
        best_gap       = gap;
      }
    }

    if (best == SourceCount) {
      // No source is eligible, because the remaining sources depend on each other. A chain never reaches this point:
      // each step reads only the columns before it, so the chain's own order always works. Masks made up elsewhere,
      // as in bench/plan/, can reach it, and the remaining places are marked as unplaced.
      for (; step < SourceCount; ++step) {
        order[step] = SourceCount;
      }
      break;
    }
    order[step]  = best;
    placed[best] = true;
    bound |= produces[best];
  }
  return order;
}

}  // namespace dreamor::internal::inline index_set
