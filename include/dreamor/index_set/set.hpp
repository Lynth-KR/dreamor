// SPDX-License-Identifier: Apache-2.0
// File: set.hpp
// The index set builder: makeSet(...).where(...).collect().
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"
#include "dreamor/general/tuple_hash.hpp"
#include "dreamor/index_set/binding.hpp"
#include "dreamor/index_set/column.hpp"
#include "dreamor/index_set/element.hpp"
#include "dreamor/index_set/grouped.hpp"
#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/plan.hpp"
#include "dreamor/logger/logger_light.hpp"

namespace dreamor {

namespace internal {
template <typename Row, typename Key>
struct GroupedOn;
template <typename Row, typename... Columns>
struct GroupedOn<Row, std::tuple<Columns...>> {
  using type = Grouped<Row, Columns...>;
};

class SourceChecks;

#ifndef NDEBUG
// The first repeat among keys: {the key held first, the one repeating it}. One instance per key
// type, not per column: keys are what a column holds, its pointer as const void*.
template <typename Key>
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> firstRepeat(const std::vector<Key>& keys) {
  ::dreamor::general::FlatIndex<Key> index{keys.size()};
  const auto repeat = index.insertAll(0, keys.size(), [&keys](std::size_t at) -> const Key& { return keys[at]; });
  if (repeat.first == ::dreamor::general::FlatIndex<Key>::kNotFound) {
    return std::nullopt;
  }
  return repeat;
}

// One message for every broken source, out of line and not a template: a build formats it once
// rather than once per column.
enum class RepeatIn : std::uint8_t { kContainer, kSpread, kDependent };
[[noreturn]] inline void reportRepeat(RepeatIn where, std::size_t source, std::pair<std::size_t, std::size_t> at) {
  switch (where) {
    case RepeatIn::kContainer:
      DREAMOR_LOG_FATAL(
          "makeSet: source {} holds one element twice, at {} and {} - a broken contract: no source "
          "holds an element twice (dreamor/index_set.hpp).",
          source,
          at.first,
          at.second);
    case RepeatIn::kSpread:
      DREAMOR_LOG_FATAL(
          "makeSet: source {} holds one row twice, at {} and {} - a broken contract: no source holds "
          "an element twice (dreamor/index_set.hpp).",
          source,
          at.first,
          at.second);
    case RepeatIn::kDependent:
      DREAMOR_LOG_FATAL(
          "makeSet: dependent source {} returned one element twice, at {} and {} - a broken contract: "
          "no source holds an element twice (dreamor/index_set.hpp).",
          source,
          at.first,
          at.second);
  }
  std::abort();
}

// What a debug build checks of the contract every set rests on (index_set.hpp): no source
// holds an element twice. A container or a spread range is checked once per collect(), a set
// not at all - it holds no row twice already. A dependent source's range is checked each time
// it is returned, unless it is borrowed (std::ranges::borrowed_range) and contiguous: its
// elements then outlive the call, so the same address and size is the same range, checked once.
class SourceChecks {
 public:
  template <typename Column, typename Range>
  void container(std::size_t source, Range& range) {
    if (const auto repeat = firstRepeat(keysOf<Column>(range))) {
      reportRepeat(RepeatIn::kContainer, source + 1, *repeat);
    }
  }

  template <typename Range>
  void spread(std::size_t source, Range& range) {
    using Row = std::ranges::range_value_t<Range>;
    if (const auto repeat = firstRepeat(std::vector<Row>(std::ranges::begin(range), std::ranges::end(range)))) {
      reportRepeat(RepeatIn::kSpread, source + 1, *repeat);
    }
  }

  // Range is the type the source returned: an lvalue reference, or the value itself.
  template <typename Column, typename Range>
  void dependent(std::size_t source, std::remove_reference_t<Range>& range) {
    if constexpr (std::ranges::borrowed_range<Range> && std::ranges::contiguous_range<Range> &&
                  std::ranges::sized_range<Range>) {
      if (!checked_.emplace(static_cast<const void*>(std::ranges::data(range)), std::ranges::size(range)).second) {
        return;
      }
    }
    if (const auto repeat = firstRepeat(keysOf<Column>(range))) {
      reportRepeat(RepeatIn::kDependent, source + 1, *repeat);
    }
  }

 private:
  // What the column holds, as the key the repeat check compares.
  template <typename Column>
  using KeyOf = std::conditional_t<std::is_pointer_v<typename Column::Value>, const void*, typename Column::Value>;

  template <typename Column, typename Range>
  [[nodiscard]] static std::vector<std::tuple<KeyOf<Column>>> keysOf(Range& range) {
    std::vector<std::tuple<KeyOf<Column>>> keys;
    if constexpr (std::ranges::sized_range<Range>) {
      keys.reserve(std::ranges::size(range));
    }
    for (auto&& element : range) {
      keys.emplace_back(toColumn<Column>(element).get());
    }
    return keys;
  }

  std::unordered_set<std::tuple<const void*, std::size_t>, ::dreamor::general::TupleHash> checked_;
};
#endif  // NDEBUG
}  // namespace internal

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
    static_assert(
        internal::PlacementOf<Pred, ElemTuple>::no_repeated_column,
        "This callable takes the same column twice, so both parameters would get one value: give each its own "
        "column - `[](FromCol a, ToCol b)` - or take the column once.");
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
#ifndef NDEBUG
    internal::SourceChecks checks;
    checks_ = &checks;
    checkSources(std::make_index_sequence<kSourceCount>{});
#endif
    indexJoins(std::make_index_sequence<kSourceCount>{});
    std::vector<ElemTuple> out;
    generate<0, std::index_sequence<>>(out);
    checks_ = nullptr;
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

  template <std::size_t Source>
  static constexpr bool kIsSpread =
      internal::sourceKindOf<std::tuple_element_t<Source, RangeTuple>>() == internal::SourceKind::kSpread;

  // Where a spread source's columns sit in the element tuple, found by type - which also
  // finds a column it shares with another spread source.
  template <std::size_t Source>
  static constexpr auto kSpreadPositions = [] {
    using SourceRow = std::ranges::range_value_t<std::tuple_element_t<Source, RangeTuple>>;
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      return std::array<std::size_t, sizeof...(Is)>{
          internal::indexOfColumn<std::tuple_element_t<Is, SourceRow>, ElemTuple>()...};
    }(std::make_index_sequence<std::tuple_size_v<SourceRow>>{});
  }();

  // The column each other source fills. The element tuple is built in declaration order, so
  // the k-th of them owns the k-th column that no spread source brings.
  static constexpr std::array<std::size_t, kSourceCount> kOwnColumn = [] {
    std::array<bool, kArity + 1> spread{};  // + 1: an absent column resolves to kArity
    std::array<bool, kSourceCount> is_spread{};
    [&]<std::size_t... Ss>(std::index_sequence<Ss...>) {
      (
          [&] {
            is_spread[Ss] = kIsSpread<Ss>;
            if constexpr (kIsSpread<Ss>) {
              for (const std::size_t at : kSpreadPositions<Ss>) {
                spread[at] = true;
              }
            }
          }(),
          ...);
    }(std::make_index_sequence<kSourceCount>{});

    std::array<std::size_t, kSourceCount> own{};
    std::size_t next = 0;
    for (std::size_t source = 0; source < kSourceCount; ++source) {
      if (is_spread[source]) {
        continue;
      }
      while (next < kArity && spread[next]) {
        ++next;
      }
      own[source] = next++;
    }
    return own;
  }();

  // Where each source's columns sit in the element tuple, in the source's own order.
  // Declaration order, not visit order: the result tuple is always the one the caller
  // declared.
  template <std::size_t Source>
  static constexpr auto kColumnsOf = [] {
    if constexpr (kIsSpread<Source>) {
      return kSpreadPositions<Source>;
    } else {
      return std::array<std::size_t, 1>{kOwnColumn[Source]};
    }
  }();

  template <std::size_t Source>
  static constexpr std::uint64_t kProduces = [] {
    std::uint64_t mask = 0;
    for (const std::size_t at : kColumnsOf<Source>) {
      mask |= std::uint64_t{1} << at;
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
                                                      std::tuple_element_t<kColumnsOf<Source>[0], ElemTuple>>();
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
  static_assert(internal::dependentSourcesDistinct<RangeTuple, ElemTuple>(),
                "A dependent column takes the same column twice, so both parameters would get one value: give each "
                "its own column, or take the column once.");
  static_assert(std::ranges::all_of(kVisitOrder, [](std::size_t source) { return source < kSourceCount; }),
                "No order of the sources satisfies their dependencies: two dependent columns need "
                "each other. A dependent column can only read columns that some other source "
                "produces on its own.");

  // --- Joins --------------------------------------------------------------------------
  // A spread source visited after some of its columns are bound - by a spread source that
  // shares them - is joined on those columns: looked up in an index built once per collect()
  // instead of scanned and compared row by row.

  // The columns already bound when each source is visited.
  static constexpr std::array<std::uint64_t, kSourceCount> kBoundBefore = [] {
    const auto produces = []<std::size_t... Ss>(std::index_sequence<Ss...>) {
      return std::array<std::uint64_t, kSourceCount>{kProduces<Ss>...};
    }(std::make_index_sequence<kSourceCount>{});
    std::array<std::uint64_t, kSourceCount> before{};
    std::uint64_t bound = 0;
    for (const std::size_t source : kVisitOrder) {
      if (source >= kSourceCount) {
        break;  // No order exists; reported above.
      }
      before[source] = bound;
      bound |= produces[source];
    }
    return before;
  }();

  template <std::size_t Source>
  static constexpr bool kJoins = kIsSpread<Source> && (kProduces<Source> & kBoundBefore[Source]) != 0;

  // The key a joined source is looked up by: its columns bound before it, in its own order.
  template <std::size_t Source, typename SourceRow, std::size_t... Is>
  static auto joinKeyOf(std::index_sequence<Is...> /*unused*/) -> decltype(std::tuple_cat(
      std::declval<std::conditional_t<((kBoundBefore[Source] >> kColumnsOf<Source>[Is]) & 1U) != 0,
                                      std::tuple<std::tuple_element_t<Is, SourceRow>>,
                                      std::tuple<>>>()...));

  // A joined source grouped by its key: groupBy's CSR rows and flat index are the join index.
  template <std::size_t Source, bool = kJoins<Source>>
  struct JoinIndex {
    using type = std::tuple<>;  // Scanned, not looked up.
  };
  template <std::size_t Source>
  struct JoinIndex<Source, true> {
    using SourceRow = std::ranges::range_value_t<std::tuple_element_t<Source, RangeTuple>>;
    using Key       = decltype(joinKeyOf<Source, SourceRow>(std::make_index_sequence<std::tuple_size_v<SourceRow>>{}));
    using type      = std::optional<typename internal::GroupedOn<SourceRow, Key>::type>;
  };

  template <typename Seq>
  struct JoinIndexes;
  template <std::size_t... Ss>
  struct JoinIndexes<std::index_sequence<Ss...>> {
    using type = std::tuple<typename JoinIndex<Ss>::type...>;
  };

#ifndef NDEBUG
  // The containers and spread ranges, once: a debug build's check of the contract.
  template <std::size_t... Ss>
  void checkSources(std::index_sequence<Ss...> /*unused*/) {
    (checkSource<Ss>(), ...);
  }

  template <std::size_t Source>
  void checkSource() {
    using Src = std::tuple_element_t<Source, RangeTuple>;
    if constexpr (internal::sourceKindOf<Src>() == internal::SourceKind::kContainer) {
      checks_->template container<std::tuple_element_t<kColumnsOf<Source>[0], ElemTuple>>(Source,
                                                                                          std::get<Source>(ranges_));
    } else if constexpr (internal::sourceKindOf<Src>() == internal::SourceKind::kSpread) {
      if constexpr (!std::same_as<Src, IndexSet<std::ranges::range_value_t<Src>>>) {
        checks_->spread(Source, std::get<Source>(ranges_));
      }
    }
  }

#endif

  template <std::size_t... Ss>
  void indexJoins(std::index_sequence<Ss...> /*unused*/) {
    (indexJoin<Ss>(), ...);
  }

  template <std::size_t Source>
  void indexJoin() {
    if constexpr (kJoins<Source>) {
      using SourceRow    = JoinIndex<Source>::SourceRow;
      const auto& source = std::get<Source>(ranges_);
      if constexpr (std::same_as<std::remove_cvref_t<decltype(source)>, IndexSet<SourceRow>>) {
        std::get<Source>(indexes_).emplace(source);  // Shares the set's rows.
      } else {
        std::vector<SourceRow> rows;
        for (const auto& row : source) {
          rows.push_back(row);
        }
        std::get<Source>(indexes_).emplace(internal::SetAccess::make(std::move(rows)));
      }
    }
  }

  // The same key, read off the values bound so far.
  template <typename Key, typename BoundCols, typename... Bound>
  [[nodiscard]] static Key boundKey(const Bound&... bound) {
    const auto refs = std::tie(bound...);
    return [&]<typename... Ks>(std::type_identity<std::tuple<Ks...>> /*unused*/) {
      return Key{
          std::get<internal::positionInSequence<internal::indexOfColumn<Ks, ElemTuple>(), BoundCols>()>(refs)...};
    }(std::type_identity<Key>{});
  }

  // Walks the sources in visit order, binding one column at a time. BoundCols names the
  // element-tuple column of each bound value, in the order they were bound.
  template <std::size_t Step, typename BoundCols, typename... Bound>
  void generate(std::vector<ElemTuple>& out, const Bound&... bound) {
    if constexpr (Step == kSourceCount) {
      static_assert(sizeof...(Bound) == kArity, "Sources and columns disagree.");
      emit<BoundCols>(out, std::tie(bound...), std::make_index_sequence<kArity>{});
    } else {
      constexpr std::size_t kSource        = kVisitOrder[Step];
      using Source                         = std::tuple_element_t<kSource, RangeTuple>;
      constexpr internal::SourceKind kKind = internal::sourceKindOf<Source>();

      if constexpr (kKind == internal::SourceKind::kSpread) {
        visitSpread<Step, kSource, BoundCols>(out, bound...);
      } else {
        constexpr std::size_t kCol = kColumnsOf<kSource>[0];
        const auto& source         = std::get<kSource>(ranges_);
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
#ifndef NDEBUG
        if constexpr (kKind == internal::SourceKind::kDependent) {
          checks_->template dependent<std::tuple_element_t<kCol, ElemTuple>, decltype(range)>(kSource, range);
        }
#endif
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

  // A joined source walks only the rows that match what is bound; any other spread source
  // walks all of its rows.
  template <std::size_t Step, std::size_t Source, typename BoundCols, typename... Bound>
  void visitSpread(std::vector<ElemTuple>& out, const Bound&... bound) {
    if constexpr (kJoins<Source>) {
      if (const auto& grouped = std::get<Source>(indexes_)) {  // Built by collect() before generate().
        for (const auto& tuple : grouped->slice(boundKey<typename JoinIndex<Source>::Key, BoundCols>(bound...))) {
          spread<Step, Source, 0, BoundCols>(out, tuple, bound...);
        }
      }
    } else {
      for (const auto& tuple : std::get<Source>(ranges_)) {
        spread<Step, Source, 0, BoundCols>(out, tuple, bound...);
      }
    }
  }

  // Unpacks one tuple of a spread source column by column, so predicates still see every
  // intermediate partial tuple. A column bound already is one the join index matched.
  template <std::size_t Step, std::size_t Source, std::size_t I, typename BoundCols, typename Tup, typename... Bound>
  void spread(std::vector<ElemTuple>& out, const Tup& tuple, const Bound&... bound) {
    if constexpr (I == std::tuple_size_v<Tup>) {
      generate<Step + 1, BoundCols>(out, bound...);
    } else {
      constexpr std::size_t kCol = kColumnsOf<Source>[I];
      if constexpr (((internal::maskOfSequence<BoundCols>() >> kCol) & 1U) != 0) {
        spread<Step, Source, I + 1, BoundCols>(out, tuple, bound...);
      } else {
        using Next        = internal::AppendColumn<BoundCols, kCol>::type;
        const auto& value = std::get<I>(tuple);
        if (!passes<kCol, Next>(bound..., value)) {
          return;
        }
        spread<Step, Source, I + 1, Next>(out, tuple, bound..., value);
      }
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
  JoinIndexes<std::make_index_sequence<kSourceCount>>::type indexes_;
  internal::SourceChecks* checks_ = nullptr;  // a debug build's, while collect() runs
};

// Each argument is a source of columns; each explicit type names one NEW column:
//
//     {m in MACHINES, i in ITEMS}          makeSet<MachineCol, ItemCol>(machines, items)
//     {m in MACHINES, i in ITEMS[m]}       makeSet<MachineCol, ItemCol>(machines, items_of)
//     {(m,i) in ASSIGN, o in ORDERS}       makeSet<OrderCol>(sASSIGN, orders)
//     {(m,i) in ROUTES, (i,t) in OPEN}     makeSet<>(sROUTES, sOPEN)
//
// Two sets that share a column are joined on it, as in AMPL: the one visited second is
// looked up by the shared columns rather than scanned.
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
  using ElemTuple = internal::
      BuildElem<std::tuple<>, std::tuple<>, std::tuple<Columns...>, std::remove_reference_t<Sources>...>::type;
  static_assert(std::tuple_size_v<ElemTuple> <= internal::kMaxColumns,
                "Congratulations on a set of more than 64 dimensions - dreamor stops at 64, and even 16 is a lot: "
                "split it into sets that share key columns, or put a hand on your heart and ask whether the model "
                "needs them all.");
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
                "and `using ToCol = Col<const Node*, \"to\">;`. Only two set sources may share "
                "a column - that joins them on it; a column named in makeSet<...> is always new.");
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
