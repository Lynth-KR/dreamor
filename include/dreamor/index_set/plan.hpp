// SPDX-License-Identifier: Apache-2.0
// File: plan.hpp
// The order sources are visited in, and the bookkeeping that lets generation bind columns
// in that order rather than the declared one.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <utility>

#include "dreamor/index_set/binding.hpp"
#include "dreamor/index_set/element.hpp"

namespace dreamor::internal {

// --- Bound columns --------------------------------------------------------------------
// Generation binds columns in visit order, which need not be element-tuple order, so the
// bound values are carried alongside an index_sequence naming the column each one is. These
// map an element-tuple column to its position in that pack.

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

// Every dependent source, checked before the plan is built: an unreadable or misspelled
// signature has no columns, which the planner would otherwise report as a cycle.
template <typename RangeTuple, typename ElemTuple, std::size_t I = 0>
[[nodiscard]] consteval bool dependentSourcesReadable() {
  if constexpr (I >= std::tuple_size_v<RangeTuple>) {
    return true;
  } else {
    using Source = std::tuple_element_t<I, RangeTuple>;
    if constexpr (sourceKindOf<Source>() == SourceKind::kDependent && no_indirect_call<Source>) {
      return ColumnsOfPredicate<Source>::known && dependentSourcesReadable<RangeTuple, ElemTuple, I + 1>();
    } else {
      return dependentSourcesReadable<RangeTuple, ElemTuple, I + 1>();
    }
  }
}

template <typename RangeTuple, typename ElemTuple, std::size_t I = 0>
[[nodiscard]] consteval bool dependentSourcesNamed() {
  if constexpr (I >= std::tuple_size_v<RangeTuple>) {
    return true;
  } else {
    using Source = std::tuple_element_t<I, RangeTuple>;
    if constexpr (sourceKindOf<Source>() == SourceKind::kDependent && ColumnsOfPredicate<Source>::known) {
      return SourcePlacement<Source, ElemTuple>::all_named && SourcePlacement<Source, ElemTuple>::all_present &&
             dependentSourcesNamed<RangeTuple, ElemTuple, I + 1>();
    } else {
      return dependentSourcesNamed<RangeTuple, ElemTuple, I + 1>();
    }
  }
}

// --- Join order -------------------------------------------------------------------------
// Sources are visited so that predicates run as early as possible. Greedy, over column
// names only - no cardinality:
//
//   1. A source is eligible once every column it depends on is bound.
//   2. Prefer the source that lets the most predicates run immediately.
//   3. Then the smallest gap - fewest columns still missing for some predicate.
//   4. Then declaration order.
//
// This changes which column varies fastest, so it changes the row order of the result.
// Set::kVisitOrder holds the plan that ran.

[[nodiscard]] consteval std::size_t popcountOf(std::uint64_t mask) {
  std::size_t n = 0;
  for (; mask != 0; mask &= mask - 1) {
    ++n;
  }
  return n;
}

template <std::size_t SourceCount, std::size_t PredCount>
// NOLINTNEXTLINE(readability-function-cognitive-complexity): one fixpoint loop; split, it no longer reads as one
[[nodiscard]] consteval std::array<std::size_t, SourceCount> planVisitOrder(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): produces and depends are both column masks per source
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
      if (placed[source] || (depends[source] & ~bound) != 0) {
        continue;
      }
      const std::uint64_t next = bound | produces[source];

      std::size_t activated = 0;
      std::size_t gap       = ~std::size_t{0};
      for (std::size_t p = 0; p < PredCount; ++p) {
        const std::uint64_t missing_now = predicates[p] & ~next;
        if (missing_now == 0) {
          // Ran already, or runs on this source; only the latter counts as progress.
          if ((predicates[p] & ~bound) != 0) {
            ++activated;
          }
        } else {
          gap = (popcountOf(missing_now) < gap) ? popcountOf(missing_now) : gap;
        }
      }
      if (gap == ~std::size_t{0}) {
        gap = 0;  // Nothing left to wait for.
      }

      const bool better =
          (best == SourceCount) || (activated > best_activated) || (activated == best_activated && gap < best_gap);
      if (better) {
        best           = source;
        best_activated = activated;
        best_gap       = gap;
      }
    }

    if (best == SourceCount) {
      // No source is eligible: the dependent sources need each other. Mark the rest
      // unplaced and let the caller turn that into a diagnostic.
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

}  // namespace dreamor::internal
