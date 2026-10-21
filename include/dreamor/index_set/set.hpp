// SPDX-License-Identifier: Apache-2.0
// File: set.hpp
// The index set builder: makeSet(...).where(...).collect().
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/index_set/binding.hpp"
#include "dreamor/index_set/column.hpp"
#include "dreamor/index_set/element.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/plan.hpp"

namespace dreamor {

// Lazy builder. Nothing is generated until collect().
template <typename ElemTuple, typename RangeTuple, typename PredTuple>
class Set {
 public:
  static constexpr std::size_t kArity = std::tuple_size_v<ElemTuple>;

  // Built by makeSet() and rebuilt by where(); not meant to be spelled out at call sites.
  Set(RangeTuple ranges, PredTuple preds) : ranges_{std::move(ranges)}, preds_{std::move(preds)} {}

  // Neither the parameter order nor the chaining order matters.
  template <typename Pred>
  [[nodiscard]] auto where(Pred pred) const {
    static_assert(internal::no_indirect_call<Pred>,
                  "A bare function name decays to a function pointer and std::function "
                  "type-erases; either is an indirect call per candidate tuple. Pass a "
                  "lambda or a function object.");
    static_assert(internal::ColumnsOfPredicate<Pred>::known,
                  "Cannot read this predicate's parameter list. Give it one concrete "
                  "signature: `[](auto, auto)` does not say which columns it reads. A "
                  "callable with a template operator() specializes PredicateColumns "
                  "instead.");
    static_assert(internal::PlacementOf<Pred, ElemTuple>::all_named,
                  "Take the columns themselves: `[](ItemCol i, OrderCol o)`, not "
                  "`[](const Item* i, const Order* o)`. Col converts to its value "
                  "implicitly, so only the parameter list changes.");
    static_assert(internal::PlacementOf<Pred, ElemTuple>::all_present,
                  "The predicate names a column this set does not have.");
    static_assert(!internal::ColumnsOfPredicate<Pred>::known ||
                      std::tuple_size_v<typename internal::ColumnsOfPredicate<Pred>::type> != 0,
                  "The predicate reads no columns, so it would be a constant. Take the "
                  "columns it tests.");
    auto next = std::tuple_cat(preds_, std::make_tuple(std::move(pred)));
    return Set<ElemTuple, RangeTuple, decltype(next)>{ranges_, std::move(next)};
  }

  // Non-const on purpose: views such as filter_view have no const begin(). This is a
  // one-shot factory call, not an observer.
  [[nodiscard]] IndexSet<ElemTuple> collect() {
    std::vector<ElemTuple> out;
    generate<0, std::index_sequence<>>(out);
    return internal::SetAccess::make(std::move(out));
  }

 private:
  // Runs every predicate whose columns are all bound and one of which is the column just
  // bound - so each predicate fires exactly once, at the earliest point it can.
  template <std::size_t JustBound, typename BoundCols, typename... Bound>
  [[nodiscard]] bool passes(const Bound&... bound) const {
    return std::apply([&](const auto&... preds) { return (checkOne<JustBound, BoundCols>(preds, bound...) && ...); },
                      preds_);
  }

  template <std::size_t JustBound, typename BoundCols, typename Pred, typename... Bound>
  [[nodiscard]] static bool checkOne(const Pred& pred, const Bound&... bound) {
    constexpr std::uint64_t kPred  = internal::PlacementOf<Pred, ElemTuple>::mask;
    constexpr std::uint64_t kBound = internal::maskOfSequence<BoundCols>();
    if constexpr ((kPred & ~kBound) == 0 && ((kPred >> JustBound) & 1U) != 0) {
      return callColumns(
          pred,
          std::tie(bound...),
          typename internal::RemapToBound<typename internal::PlacementOf<Pred, ElemTuple>::indices, BoundCols>::type{});
    } else {
      return true;
    }
  }

  // Calls a predicate or a dependent source with just the columns it named.
  template <typename Callable, typename BoundRefs, std::size_t... Is>
  [[nodiscard]] static decltype(auto) callColumns(const Callable& callable,
                                                  const BoundRefs& bound,
                                                  std::index_sequence<Is...> /*unused*/) {
    return callable(std::get<Is>(bound)...);
  }

  static constexpr std::size_t kSourceCount = std::tuple_size_v<RangeTuple>;

  // Where each source's columns sit in the element tuple. Declaration order, not visit
  // order: the result tuple is always the one the caller declared.
  static constexpr std::array<std::size_t, kSourceCount> kColumnStart = [] {
    std::array<std::size_t, kSourceCount> starts{};
    std::size_t next = 0;
    [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      ((starts[Is] = next, next += internal::columnCountOf<std::tuple_element_t<Is, RangeTuple>>()), ...);
    }(std::make_index_sequence<kSourceCount>{});
    return starts;
  }();

  template <std::size_t Source>
  static constexpr std::uint64_t kProduces = [] {
    constexpr std::size_t kCount = internal::columnCountOf<std::tuple_element_t<Source, RangeTuple>>();

    std::uint64_t mask = 0;
    for (std::size_t i = 0; i < kCount; ++i) {
      mask |= std::uint64_t{1} << (kColumnStart[Source] + i);
    }
    return mask;
  }();

  // Pointers into the model, not copies of it.
  template <std::size_t Source>
  static constexpr bool kYieldsPointers = [] {
    using Src = std::tuple_element_t<Source, RangeTuple>;
    if constexpr (internal::sourceKindOf<Src>() == internal::SourceKind::kDependent &&
                  internal::ColumnsOfPredicate<Src>::known) {
      using Ret = internal::ReturnTypeOf<const Src&, typename internal::ColumnsOfPredicate<Src>::type>;
      if constexpr (Ret::invocable) {
        return internal::dependentRangeYieldsPointers<typename Ret::type,
                                                      std::tuple_element_t<kColumnStart[Source], ElemTuple>>();
      } else {
        return true;  // Other asserts report a source that is not callable at all.
      }
    } else {
      return true;
    }
  }();

  template <std::size_t Source>
  static constexpr std::uint64_t kDepends = [] {
    using Src = std::tuple_element_t<Source, RangeTuple>;
    if constexpr (internal::sourceKindOf<Src>() == internal::SourceKind::kDependent &&
                  internal::ColumnsOfPredicate<Src>::known) {
      return internal::SourcePlacement<Src, ElemTuple>::mask;
    } else {
      return std::uint64_t{0};
    }
  }();

 public:
  // The order sources are visited in, as declaration indices. Exposed so a model can log
  // the plan it actually ran.
  static constexpr std::array<std::size_t, kSourceCount> kVisitOrder = [] {
    return [&]<std::size_t... Ss, std::size_t... Ps>(std::index_sequence<Ss...>, std::index_sequence<Ps...>) {
      return internal::planVisitOrder<kSourceCount, sizeof...(Ps)>(
          {kProduces<Ss>...},
          {kDepends<Ss>...},
          {internal::PlacementOf<std::tuple_element_t<Ps, PredTuple>, ElemTuple>::mask...});
    }(std::make_index_sequence<kSourceCount>{}, std::make_index_sequence<std::tuple_size_v<PredTuple>>{});
  }();

 private:
  static_assert(internal::dependentSourcesReadable<RangeTuple, ElemTuple>(),
                "A dependent column needs one concrete signature: `[](MachineCol m) { ... }`. "
                "A generic or `mutable` lambda has no readable parameter list - the source "
                "runs once per binding, so it has to be callable as const.");
  static_assert(
      [] {
        return [&]<std::size_t... Ss>(std::index_sequence<Ss...>) {
          return (kYieldsPointers<Ss> && ...);
        }(std::make_index_sequence<kSourceCount>{});
      }(),
      "A dependent column yields pointers to the model's entities, not the entities: "
      "`std::vector<const Item*>`, not `std::vector<Item>`. Entities by value are copies, "
      "so a membership test or a VarTable lookup against them finds nothing - and they die "
      "when that binding's loop ends.");
  static_assert(internal::dependentSourcesNamed<RangeTuple, ElemTuple>(),
                "A dependent column takes the columns themselves: `[](MachineCol m)`, not "
                "`[](const Machine* m)`, and only columns this set has.");
  static_assert(std::ranges::all_of(kVisitOrder, [](std::size_t source) { return source < kSourceCount; }),
                "No order of the sources satisfies their dependencies: two dependent columns need "
                "each other. A dependent column can only read columns that some other source "
                "produces on its own.");

  // Walks the sources in visit order, binding one column at a time. BoundCols names the
  // element-tuple column of each bound value, in the order they were bound.
  template <std::size_t Step, typename BoundCols, typename... Bound>
  void generate(std::vector<ElemTuple>& out, const Bound&... bound) {
    if constexpr (Step == kSourceCount) {
      static_assert(sizeof...(Bound) == kArity, "Sources and columns disagree.");
      emit<BoundCols>(out, std::tie(bound...), std::make_index_sequence<kArity>{});
    } else {
      constexpr std::size_t kSource        = kVisitOrder[Step];
      constexpr std::size_t kCol           = kColumnStart[kSource];
      using Source                         = std::tuple_element_t<kSource, RangeTuple>;
      constexpr internal::SourceKind kKind = internal::sourceKindOf<Source>();
      const auto& source                   = std::get<kSource>(ranges_);

      if constexpr (kKind == internal::SourceKind::kSpread) {
        for (const auto& tuple : source) {
          spread<Step, kCol, 0, BoundCols>(out, tuple, bound...);
        }
      } else {
        // `auto&&` keeps a returned-by-value range alive for the loop.
        auto&& range = [&]() -> decltype(auto) {
          if constexpr (kKind == internal::SourceKind::kDependent) {
            // Signature and column checks live at class scope, so they report before any
            // of this is instantiated.
            using Placement = internal::SourcePlacement<Source, ElemTuple>;
            static_assert((Placement::mask & ~internal::maskOfSequence<BoundCols>()) == 0,
                          "This source reads a column that is not bound when it runs.");
            return callColumns(source,
                               std::tie(bound...),
                               typename internal::RemapToBound<typename Placement::indices, BoundCols>::type{});
          } else {
            return source;
          }
        }();
        using Next = internal::AppendColumn<BoundCols, kCol>::type;
        for (auto&& element : range) {
          const auto value = internal::toColumn<std::tuple_element_t<kCol, ElemTuple>>(element);
          if (!passes<kCol, Next>(bound..., value)) {
            continue;  // Prunes every deeper loop for this partial tuple.
          }
          generate<Step + 1, Next>(out, bound..., value);
        }
      }
    }
  }

  // Unpacks one tuple of a spread source column by column, so predicates still see every
  // intermediate partial tuple.
  template <std::size_t Step, std::size_t Col, std::size_t I, typename BoundCols, typename Tup, typename... Bound>
  void spread(std::vector<ElemTuple>& out, const Tup& tuple, const Bound&... bound) {
    if constexpr (I == std::tuple_size_v<Tup>) {
      generate<Step + 1, BoundCols>(out, bound...);
    } else {
      using Next        = internal::AppendColumn<BoundCols, Col + I>::type;
      const auto& value = std::get<I>(tuple);
      if (!passes<Col + I, Next>(bound..., value)) {
        return;
      }
      spread<Step, Col, I + 1, Next>(out, tuple, bound..., value);
    }
  }

  // Reassembles into declaration order: the visit order is an implementation detail, the
  // result tuple is not.
  template <typename BoundCols, typename BoundRefs, std::size_t... Is>
  static void emit(std::vector<ElemTuple>& out, const BoundRefs& bound, std::index_sequence<Is...> /*unused*/) {
    out.emplace_back(std::get<internal::positionInSequence<Is, BoundCols>()>(bound)...);
  }

  RangeTuple ranges_;
  PredTuple preds_;
};

// Each argument is a source of columns; each explicit type names one NEW column:
//
//     {m in MACHINES, i in ITEMS}          makeSet<MachineCol, ItemCol>(machines, items)
//     {m in MACHINES, i in ITEMS[m]}       makeSet<MachineCol, ItemCol>(machines, items_of)
//     {(m,i) in ASSIGN, o in ORDERS}       makeSet<OrderCol>(sASSIGN, orders)
//
// Containers, not lazy views: generation is a nested loop, so every source after the first
// is visited once per binding and a filtered view would re-run its predicate each time.
// Filter with .where(), which prunes partial tuples instead.
//
// A container must be an lvalue - the tuples hold pointers into it. A dependent column is
// held by value, so it may be a temporary.
template <typename... Columns, typename... Sources>
[[nodiscard]] auto makeSet(Sources&&... sources) {
  static_assert(sizeof...(Sources) > 0, "makeSet needs at least one source.");
  static_assert(((internal::sourceKindOf<std::remove_reference_t<Sources>>() == internal::SourceKind::kDependent ||
                  std::is_lvalue_reference_v<Sources>) &&
                 ...),
                "A container source must be a named variable, not a temporary: the tuples "
                "hold pointers into it, so it has to outlive the set.");
  static_assert(((internal::sourceKindOf<std::remove_reference_t<Sources>>() != internal::SourceKind::kDependent ||
                  internal::no_indirect_call<std::remove_cvref_t<Sources>>) &&
                 ...),
                "A dependent column cannot be a bare function name or a std::function: either is an "
                "indirect call per binding. Pass a lambda or a function object.");
  using ElemTuple =
      internal::BuildElem<std::tuple<>, std::tuple<Columns...>, std::remove_reference_t<Sources>...>::type;
  static_assert((internal::is_column<Columns> && ...),
                "Every column needs a name: makeSet<MachineCol, ItemCol>(machines, items), not "
                "makeSet<const Machine*, const Item*>(...). Declare each one once with "
                "`using MachineCol = Col<const Machine*, \"machine\">;`.");
  static_assert(internal::allElementsAreColumns<ElemTuple>(std::make_index_sequence<std::tuple_size_v<ElemTuple>>{}),
                "A column of this set is not a Col. A spread source brings its own column "
                "types, so the set it spreads has to have been built with named columns "
                "too - a hand-made vector<tuple<int, int>> will not do.");
  static_assert(internal::allColumnsDistinct<ElemTuple>(std::make_index_sequence<std::tuple_size_v<ElemTuple>>{}),
                "The same column type appears twice, so a type no longer identifies one "
                "column. Give each its own: `using FromCol = Col<const Node*, \"from\">;` "
                "and `using ToCol = Col<const Node*, \"to\">;`.");
  static_assert(internal::allColumnNamesDistinct<ElemTuple>(std::make_index_sequence<std::tuple_size_v<ElemTuple>>{}),
                "Two columns share a name. Names have to be unique even when the value "
                "types differ, because kName is what identifies a column outside the type "
                "system - in generated variable names, for one.");
  auto source_tuple = std::make_tuple(internal::holdSource(std::forward<Sources>(sources))...);
  return Set<ElemTuple, decltype(source_tuple), std::tuple<>>{std::move(source_tuple), {}};
}

namespace internal {
template <typename Builder>
[[nodiscard]] auto applyWhere(Builder builder) {
  return builder;
}
template <typename Builder, typename Pred, typename... Preds>
[[nodiscard]] auto applyWhere(const Builder& builder, Pred pred, Preds... preds) {
  return applyWhere(builder.where(std::move(pred)), std::move(preds)...);
}
}  // namespace internal

// The rows of a set that pass every predicate - AMPL's {(m,i,o) in ASSIGN: p and q}.
//
//     const auto sLARGE = sASSIGN.where(is_large);
//     const auto sOPEN_LARGE = sASSIGN.where(is_large, is_open);
//
// Predicates name their columns exactly as in the builder's where(): it is the same
// filter, applied to an existing set instead of during generation.
template <typename Tup>
template <typename... Preds>
IndexSet<Tup> IndexSet<Tup>::where(Preds... preds) const {
  static_assert(sizeof...(Preds) > 0, "where() needs at least one predicate.");
  return internal::applyWhere(makeSet<>(*this), std::move(preds)...).collect();
}

}  // namespace dreamor
