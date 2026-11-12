// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The engine that generates a set's rows from its sources, predicates, visit order and join indexes, and the where()
// and the records constructor of IndexSet, which are built on it.
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dreamor/internal/base/flat_index.hpp"
#include "dreamor/internal/base/tuple_hash.hpp"
#include "dreamor/internal/index_set/binding.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/element.hpp"
#include "dreamor/internal/index_set/grouped.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/name_token.hpp"
#include "dreamor/internal/index_set/plan.hpp"
#include "dreamor/logger.hpp"

namespace dreamor {
template <typename T>
class EntityVector;  // Only the name is needed here. The class is in dreamor/entity_vector.hpp.
}  // namespace dreamor

namespace dreamor::internal::inline index_set {

template <typename Row, typename Key>
struct GroupedOn;

template <typename Row, typename... Columns>
struct GroupedOn<Row, std::tuple<Columns...>> {
  using type = Grouped<SetFor<Row>, Columns...>;
};

class SourceChecks;

#ifndef NDEBUG
// Returns the first repeat among keys as {the position of the key held first, the position of its repeat}. There is one
// instance for each key type, not for each column: a key is what a column holds, and an entity column's key is its
// address as const void*, so every entity column shares one instance.
template <typename Key>
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> firstRepeat(const std::vector<Key>& keys) {
  ::dreamor::internal::FlatIndex<Key> index{keys.size()};
  const auto repeat = index.insertAll(0, keys.size(), [&keys](std::size_t at) -> const Key& { return keys[at]; });
  if (repeat.first == ::dreamor::internal::FlatIndex<Key>::kNotFound) { return std::nullopt; }
  return repeat;
}

// One message for every broken source. It is out of line and not a template, so a build formats it once instead of once
// for each column.
enum class RepeatIn : std::uint8_t { kContainer, kSpread, kDependent };

[[noreturn]] inline void reportRepeat(RepeatIn where, std::size_t source, std::pair<std::size_t, std::size_t> at) {
  switch (where) {
    case RepeatIn::kContainer:
      DREAMOR_LOG_FATAL(
          "building a set: source {} holds one element twice, at {} and {} - a broken contract: no source "
          "holds an element twice (dreamor/index_set.hpp).",
          source,
          at.first,
          at.second);
    case RepeatIn::kSpread:
      DREAMOR_LOG_FATAL(
          "building a set: source {} holds one row twice, at {} and {} - a broken contract: no source holds "
          "an element twice (dreamor/index_set.hpp).",
          source,
          at.first,
          at.second);
    case RepeatIn::kDependent:
      DREAMOR_LOG_FATAL(
          "building a set: lateral source {} returned one element twice, at {} and {} - a broken contract: "
          "no source holds an element twice (dreamor/index_set.hpp).",
          source,
          at.first,
          at.second);
  }
  std::abort();
}

// One message for every source that gives a row an empty Ref, written as reportRepeat is.
[[noreturn]] inline void reportEmpty(RepeatIn where, std::size_t source, std::size_t at) {
  switch (where) {
    case RepeatIn::kContainer:
      DREAMOR_LOG_FATAL(
          "building a set: source {} holds an empty Ref at {} - a broken contract: a set's row refers to an entity "
          "in each entity column (dreamor/index_set.hpp).",
          source,
          at);
    case RepeatIn::kSpread:
      DREAMOR_LOG_FATAL(
          "building a set: source {} holds a row with an empty Ref at {} - a broken contract: a set's row refers to "
          "an entity in each entity column (dreamor/index_set.hpp).",
          source,
          at);
    case RepeatIn::kDependent:
      DREAMOR_LOG_FATAL(
          "building a set: lateral source {} returned an empty Ref at {} - a broken contract: a set's row refers "
          "to an entity in each entity column (dreamor/index_set.hpp).",
          source,
          at);
  }
  std::abort();
}

// Returns the position of the first key that refers to no entity. The key of an entity column is the address that it
// holds.
template <typename Key>
[[nodiscard]] std::optional<std::size_t> firstEmpty(const std::vector<std::tuple<Key>>& keys) {
  if constexpr (std::is_same_v<Key, const void*>) {
    for (std::size_t at = 0; at < keys.size(); ++at) {
      if (std::get<0>(keys[at]) == nullptr) { return at; }
    }
  }
  return std::nullopt;
}

// Whether a row holds an empty Ref in one of its entity columns.
template <typename Row>
[[nodiscard]] bool holdsEmptyRef(const Row& row) {
  const auto empty = [](const auto& column) {
    using Value = std::remove_cvref_t<decltype(column)>::value_type;
    if constexpr (ref<Value>) {
      return RefAccess::address(column.get()) == nullptr;
    } else {
      return false;
    }
  };
  return std::apply([&](const auto&... columns) { return (empty(columns) || ...); }, row);
}

// The checks that a debug build makes of the rows of IndexSet{records, row_of}, as reportRepeat and reportEmpty make
// for a chain's build().
[[noreturn]] inline void reportRecordsEmpty(std::size_t at) {
  DREAMOR_LOG_FATAL(
      "IndexSet{{records, row_of}}: row_of returned an empty Ref for record {} - a broken contract: a set's row "
      "refers to an entity in each entity column (dreamor/index_set.hpp).",
      at);
}

[[noreturn]] inline void reportRecordsRepeat(std::pair<std::size_t, std::size_t> at, const std::string& row) {
  DREAMOR_LOG_FATAL(
      "IndexSet{{records, row_of}}, or a ParamTable built from records: records {} and {} (counting from 0) both "
      "give the row {} - a broken contract: no source holds an element twice (dreamor/index_set.hpp).",
      at.first,
      at.second,
      row);
}

template <typename Tup>
void checkRowsOfRecords(const Rows<Tup>& rows) {
  for (std::size_t at = 0; at < rows.size(); ++at) {
    if (holdsEmptyRef(rows[at])) { reportRecordsEmpty(at); }
  }
  if (const auto repeat = firstRepeat(std::vector<Tup>(rows.begin(), rows.end()))) {
    reportRecordsRepeat(*repeat, keyText(rows[repeat->first]));
  }
}

// The column through which SourceChecks reads a source's elements. There is one for each value type, not one for each
// column. A repeat is looked for by what a column holds, not by the column's name, so every column that holds one type
// shares one check for each source type, and the sets of a model do not each compile a check of their own.
template <typename Value>
struct KeyColumn : ::dreamor::Col<KeyColumn<Value>, Value> {
  using ::dreamor::Col<KeyColumn<Value>, Value>::Col;
};

/// @contract source_holds_no_repeat: No source of a set holds an element twice. The sources include each range that
/// the callable of `lateral` returns, and each range that the callable of a generator over a new column returns. The
/// sources also include the rows that the `row_of` callable returns for the records, in the constructors of `IndexSet`
/// and of `ParamTable` that take records.

/// @contract source_holds_no_empty_ref: No source fills an entity column with an empty `Ref`.

// The checks that a debug build makes of the two contracts. A container or a spread range is checked once for each
// build(). A set is not checked, because it holds no row twice. The range that a dependent source returns is checked
// each time it is returned, unless the range is borrowed (std::ranges::borrowed_range) and contiguous. The elements of
// such a range outlive the call, so the same address and size mean the same range, and it is checked once.
class SourceChecks {
 public:
  // Value is what the source's column holds, its value_type.
  template <typename Value, typename Range>
  void container(std::size_t source, Range& range) {
    const auto keys = keysOf<Value>(range);
    if (const auto empty = firstEmpty(keys)) { reportEmpty(RepeatIn::kContainer, source + 1, *empty); }
    if (const auto repeat = firstRepeat(keys)) { reportRepeat(RepeatIn::kContainer, source + 1, *repeat); }
  }

  template <typename Range>
  void spread(std::size_t source, Range& range) {
    // Copied element by element: the iterator of a view over an interval of std::size_t is no C++17 iterator under
    // -std=c++20 (its difference type is an integer-class type), so std::vector's constructor from two takes none.
    using Row = std::ranges::range_value_t<Range>;
    std::vector<Row> rows;
    if constexpr (std::ranges::sized_range<Range>) { rows.reserve(std::ranges::size(range)); }
    for (auto&& row : range) {
      rows.emplace_back(row);
    }
    for (std::size_t at = 0; at < rows.size(); ++at) {
      if (holdsEmptyRef(rows[at])) { reportEmpty(RepeatIn::kSpread, source + 1, at); }
    }
    if (const auto repeat = firstRepeat(rows)) { reportRepeat(RepeatIn::kSpread, source + 1, *repeat); }
  }

  // Range is the type that the source returned: an lvalue reference, or the value itself.
  template <typename Value, typename Range>
  void dependent(std::size_t source, std::remove_reference_t<Range>& range) {
    if constexpr (std::ranges::borrowed_range<Range> && std::ranges::contiguous_range<Range> &&
                  std::ranges::sized_range<Range>) {
      if (!checked_.emplace(static_cast<const void*>(std::ranges::data(range)), std::ranges::size(range)).second) {
        return;
      }
    }
    const auto keys = keysOf<Value>(range);
    if (const auto empty = firstEmpty(keys)) { reportEmpty(RepeatIn::kDependent, source + 1, *empty); }
    if (const auto repeat = firstRepeat(keys)) { reportRepeat(RepeatIn::kDependent, source + 1, *repeat); }
  }

 private:
  // What the column holds, as the key that the repeat check compares. An entity column's key is its address.
  template <typename Value>
  using KeyOf = std::conditional_t<ref<Value>, const void*, Value>;

  template <typename Value, typename Range>
  [[nodiscard]] static std::vector<std::tuple<KeyOf<Value>>> keysOf(Range& range) {
    std::vector<std::tuple<KeyOf<Value>>> keys;
    if constexpr (std::ranges::sized_range<Range>) { keys.reserve(std::ranges::size(range)); }
    for (auto&& element : range) {
      if constexpr (ref<Value>) {
        keys.emplace_back(RefAccess::address(toColumn<KeyColumn<Value>>(element).get()));
      } else {
        keys.emplace_back(toColumn<KeyColumn<Value>>(element).get());
      }
    }
    return keys;
  }

  std::unordered_set<std::tuple<const void*, std::size_t>, ::dreamor::internal::TupleHash> checked_;
};
#endif  // NDEBUG

// The checks for a predicate, wherever it is given: a set's where(), and a chain's where() in relational.hpp. Each of
// those also checks that the columns which the predicate reads are there. None of these checks depends on the set's
// columns.
template <typename Pred, typename ElemTuple = std::tuple<>>
consteval void checkPredicateOf() {
  static_assert(no_indirect_call<Pred>,
                "A bare function name or a std::function is an indirect call per candidate row: pass a lambda "
                "or a function object.");
  static_assert(ColumnsOfPredicate<Pred>::known, DREAMOR_INTERNAL_READ_PARAMETERS_MESSAGE);
  static_assert(PlacementOf<Pred, ElemTuple>::all_named, DREAMOR_INTERNAL_TAKE_COLUMNS_MESSAGE);
  static_assert(ColumnsOfPredicate<Pred>::no_mutable_reference, DREAMOR_INTERNAL_READ_ONLY_COLUMNS_MESSAGE);
  static_assert(PlacementOf<Pred, ElemTuple>::no_repeated_column,
                "This callable takes the same column twice, so both parameters would get one value: give each its own "
                "column - `[](cFrom a, cTo b)` - or take the column once.");
  static_assert(!ColumnsOfPredicate<Pred>::known || std::tuple_size_v<typename ColumnsOfPredicate<Pred>::type> != 0,
                "The predicate reads no columns, so it would be a constant: take the columns it tests - "
                "`[](cMachine m)`.");
}

// A column's name as its declaration wrote it, from columnName(), which the macros define. A column declared by hand
// without columnName() has the name "?".
template <typename Column>
[[nodiscard]] consteval std::string_view columnNameOf() {
  if constexpr (requires { std::string_view{Column::columnName()}; }) {
    return Column::columnName();
  } else {
    return "?";
  }
}

// The columns of one source, as positions in the set's row, and whether the source is a set, whose columns are shown in
// braces.
template <std::size_t Arity>
struct SourceColumns {
  std::array<std::size_t, Arity> at{};
  std::size_t count = 0;
  bool spread       = false;
};

// Writes the visit order as the sources' column names, such as "cItem, cPeriod, cMachine", with a set's columns in
// braces. It passes the text to put one piece at a time, so the same walk can count the text and write it.
template <std::size_t Arity, std::size_t SourceCount, typename Put>
constexpr void writeVisitOrder(const std::array<std::size_t, SourceCount>& order,
                               const std::array<SourceColumns<Arity>, SourceCount>& sources,
                               const std::array<std::string_view, Arity>& names,
                               Put put) {
  for (std::size_t step = 0; step < SourceCount; ++step) {
    if (step != 0) { put(", "); }
    const SourceColumns<Arity>& source = sources.at(order.at(step));
    if (source.spread) { put("{"); }
    for (std::size_t i = 0; i < source.count; ++i) {
      if (i != 0) { put(", "); }
      put(names.at(source.at.at(i)));
    }
    if (source.spread) { put("}"); }
  }
}

// The engine that a chain (relational.hpp) and a set's where() build with: the sources, each filling the columns that
// it adds, and the predicates. Nothing is generated until build().
//
// ElemTuple is the row that the set holds, with its columns in the order that the caller declared: a chain's own order,
// which is the order in which its sources added them, or the head of set-builder notation. Brought is the order in
// which the sources add the same columns. A source of one column fills the column at its place in Brought, and that
// column is found in ElemTuple by type.
template <typename ElemTuple, typename RangeTuple, typename PredTuple, typename Brought = ElemTuple>
class Set {
 public:
  static constexpr std::size_t kArity = std::tuple_size_v<ElemTuple>;

  // A chain's build() and a set's where() construct the engine. No other code names it.
  Set(RangeTuple ranges, PredTuple preds) : ranges_{std::move(ranges)}, preds_{std::move(preds)} {}

  // All predicates combine with AND, whether they are given in one call or in chained calls. The order of the
  // parameters, of the predicates and of the calls does not matter.
  template <typename... Preds>
  [[nodiscard]] auto where(Preds... preds) const {
    (checkPredicate<Preds>(), ...);
    auto next = std::tuple_cat(preds_, std::make_tuple(std::move(preds)...));
    return Set<ElemTuple, RangeTuple, decltype(next), Brought>{ranges_, std::move(next)};
  }

  // build() is not const on purpose, because views such as filter_view have no const begin(). It is called once to make
  // the set, and it does more than observe the engine.
  [[nodiscard]] internal::SetFor<ElemTuple> build() {
#ifndef NDEBUG
    internal::SourceChecks checks;
    checks_ = &checks;
    checkSources(std::make_index_sequence<kSourceCount>{});
#endif
    indexJoins(std::make_index_sequence<kSourceCount>{});
    internal::Rows<ElemTuple> out;
    out.reserve(exactRows(std::make_index_sequence<kSourceCount>{}));
    generate<0, std::index_sequence<>>(out);
    checks_ = nullptr;
    return internal::SetAccess::make(std::move(out));
  }

 private:
  template <typename Pred>
  static consteval void checkPredicate() {
    internal::checkPredicateOf<Pred>();
    static_assert(internal::PlacementOf<Pred, ElemTuple>::all_present,
                  "The predicate takes a column this set does not have: take only the set's own columns - over "
                  "IndexSet<cMachine, cItem>, `[](cMachine m, cItem i)` or either alone.");
  }

  // How many rows build() makes, if that is known before it makes them. It is known if every source is a sized range of
  // its own, with no dependent column, no join and no predicate: the number of rows is then the product of the sources'
  // sizes. Reserving that number spares the vector its doublings, each of which copies every row so far into memory
  // that the system has to fault in. The result is 0 if the number is not known or does not fit in std::size_t.
  template <std::size_t... Ss>
  [[nodiscard]] std::size_t exactRows(std::index_sequence<Ss...> /*unused*/) {
    if constexpr (std::tuple_size_v<PredTuple> == 0 &&
                  ((internal::sourceKindOf<std::tuple_element_t<Ss, RangeTuple>>() !=
                        internal::SourceKind::kDependent &&
                    !kJoins<Ss> && std::ranges::sized_range<std::tuple_element_t<Ss, RangeTuple>&>) &&
                   ...)) {
      std::size_t rows = 1;
      bool fits        = true;
      (
          [&] {
            const auto n = static_cast<std::size_t>(std::ranges::size(std::get<Ss>(ranges_)));
            fits         = fits && (n == 0 || rows <= std::numeric_limits<std::size_t>::max() / n);
            rows         = fits ? rows * n : 0;
          }(),
          ...);
      return rows;
    } else {
      return 0;
    }
  }

  // Runs every predicate whose columns are all bound and one of whose columns is the column just bound. So each
  // predicate runs exactly once for a partial row, at the earliest point it can.
  template <std::size_t JustBound, typename BoundCols, typename... Bound>
  [[nodiscard]] bool passes(const Bound&... bound) const {
    return std::apply([&](const auto&... preds) { return (checkOne<JustBound, BoundCols>(preds, bound...) && ...); },
                      preds_);
  }

  template <std::size_t JustBound, typename BoundCols, typename Pred, typename... Bound>
  [[nodiscard]] static bool checkOne(const Pred& pred, [[maybe_unused]] const Bound&... bound) {
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

  // Calls a predicate or a dependent source with only the columns that it names.
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

  // Each member variable template below takes its value from a function template, not from a lambda called in its
  // initializer: Apple Clang (16 and 17, up to Xcode 26.3) substitutes into such a lambda when the class is
  // instantiated, before Source is known, and then fails on the branches of an if constexpr on Source.

  // Where a spread source's columns sit in the element tuple. They are found by type, which also finds a column that
  // the source shares with another spread source.
  template <std::size_t Source>
  static consteval auto spreadPositionsOf() {
    using SourceRow = std::ranges::range_value_t<std::tuple_element_t<Source, RangeTuple>>;
    return []<std::size_t... Is>(std::index_sequence<Is...>) {
      return std::array<std::size_t, sizeof...(Is)>{
          internal::indexOfColumn<std::tuple_element_t<Is, SourceRow>, ElemTuple>()...};
    }(std::make_index_sequence<std::tuple_size_v<SourceRow>>{});
  }

  template <std::size_t Source>
  static constexpr auto kSpreadPositions = spreadPositionsOf<Source>();

  // Where each column sits in the element tuple, in the order in which the sources added the columns.
  static constexpr std::array<std::size_t, kArity> kBroughtAt = []<std::size_t... Is>(std::index_sequence<Is...>) {
    return std::array<std::size_t, kArity>{internal::indexOfColumn<std::tuple_element_t<Is, Brought>, ElemTuple>()...};
  }(std::make_index_sequence<kArity>{});

  static constexpr bool inElemTuple(std::size_t at) noexcept { return at < kArity; }

  static_assert(std::tuple_size_v<Brought> == kArity && std::ranges::all_of(kBroughtAt, inElemTuple),
                "The sources' columns and the set's are not the same columns.");

  // The column that each source of one column fills. The sources add the columns in the order of Brought. A source of
  // one column adds a new column. A spread source adds the columns that no source before it added, and it may share the
  // others, including the column of a range or of a lateral source. So each source of one column owns the column of
  // Brought at the position equal to the number of columns added before it, wherever the element tuple puts that
  // column.
  static constexpr std::array<std::size_t, kSourceCount> kOwnColumn = [] {
    std::array<std::size_t, kSourceCount> own{};
    std::uint64_t bound = 0;
    std::size_t brought = 0;
    [&]<std::size_t... Ss>(std::index_sequence<Ss...>) {
      (
          [&] {
            if constexpr (kIsSpread<Ss>) {
              for (const std::size_t at : kSpreadPositions<Ss>) {
                if (((bound >> at) & 1U) == 0) {
                  bound |= std::uint64_t{1} << at;
                  ++brought;
                }
              }
            } else {
              own[Ss] = kBroughtAt[brought++];
              bound |= std::uint64_t{1} << own[Ss];
            }
          }(),
          ...);
    }(std::make_index_sequence<kSourceCount>{});
    return own;
  }();

  // Where each source's columns sit in the element tuple, in the source's own order. This is the declaration order, not
  // the visit order, because the result tuple is always the one that the caller declared.
  template <std::size_t Source>
  static consteval auto columnsOf() {
    if constexpr (kIsSpread<Source>) {
      return kSpreadPositions<Source>;
    } else {
      return std::array<std::size_t, 1>{kOwnColumn[Source]};
    }
  }

  template <std::size_t Source>
  static constexpr auto kColumnsOf = columnsOf<Source>();

  template <std::size_t Source>
  static consteval std::uint64_t producesOf() {
    std::uint64_t mask = 0;
    for (const std::size_t at : kColumnsOf<Source>) {
      mask |= std::uint64_t{1} << at;
    }
    return mask;
  }

  template <std::size_t Source>
  static constexpr std::uint64_t kProduces = producesOf<Source>();

  // Whether each entity column is filled from its EntityVector or from Refs (refsFromTheirVector). For a dependent
  // column, the range that the column's source returns is tested.
  template <std::size_t Source>
  static consteval bool refsFromTheirVectorOf() {
    using Src = std::tuple_element_t<Source, RangeTuple>;
    if constexpr (kIsSpread<Source>) {
      return true;  // A spread source adds its own columns.
    } else {
      using Column = std::tuple_element_t<kColumnsOf<Source>[0], ElemTuple>;
      if constexpr (internal::sourceKindOf<Src>() != internal::SourceKind::kDependent) {
        return internal::refsFromTheirVector<Src, Column>();
      } else if constexpr (internal::ColumnsOfPredicate<Src>::known) {
        using Ret = internal::ReturnTypeOf<const Src&, typename internal::ColumnsOfPredicate<Src>::type>;
        if constexpr (Ret::invocable) {
          return internal::refsFromTheirVector<typename Ret::type, Column>();
        } else {
          return true;  // Another assert reports a source that cannot be called.
        }
      } else {
        return true;
      }
    }
  }

  template <std::size_t Source>
  static constexpr bool kRefsFromTheirVector = refsFromTheirVectorOf<Source>();

  // The columns that sources of one column add: the columns of ranges and of lateral sources.
  static constexpr std::uint64_t kBroughtOne = []<std::size_t... Ss>(std::index_sequence<Ss...>) {
    return ((kIsSpread<Ss> ? std::uint64_t{0} : std::uint64_t{1} << kOwnColumn[Ss]) | ... | std::uint64_t{0});
  }(std::make_index_sequence<kSourceCount>{});

  // The columns that a source waits for. A lateral source waits for the columns that it reads. A spread source waits
  // for the columns that it shares with a source of one column, because that source binds them itself and has no rows
  // to look up by them. So the set comes after that source, and is joined on those columns through its index.
  template <std::size_t Source>
  static consteval std::uint64_t dependsOf() {
    using Src = std::tuple_element_t<Source, RangeTuple>;
    if constexpr (internal::sourceKindOf<Src>() == internal::SourceKind::kDependent &&
                  internal::ColumnsOfPredicate<Src>::known) {
      return internal::SourcePlacement<Src, ElemTuple>::mask;
    } else if constexpr (kIsSpread<Source>) {
      return kProduces<Source> & kBroughtOne;
    } else {
      return std::uint64_t{0};
    }
  }

  template <std::size_t Source>
  static constexpr std::uint64_t kDepends = dependsOf<Source>();

 public:
  // The visit order, as declaration indices. Chain::visitOrder() shows it by name.
  static constexpr std::array<std::size_t, kSourceCount> kVisitOrder = [] {
    return [&]<std::size_t... Ss, std::size_t... Ps>(std::index_sequence<Ss...>, std::index_sequence<Ps...>) {
      return internal::planVisitOrder<kSourceCount, sizeof...(Ps)>(
          {kProduces<Ss>...},
          {kDepends<Ss>...},
          {internal::PlacementOf<std::tuple_element_t<Ps, PredTuple>, ElemTuple>::mask...});
    }(std::make_index_sequence<kSourceCount>{}, std::make_index_sequence<std::tuple_size_v<PredTuple>>{});
  }();

  // The visit order as the columns' names, for a model to log (Chain::visitOrder). Its parts are made only for a chain
  // that asks for them.
  [[nodiscard]] static constexpr std::string_view visitOrderText() noexcept {
    return {kVisitText.data(), kVisitText.size()};
  }

 private:
  static constexpr std::array<std::string_view, kArity> kColumnNames =
      []<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::array<std::string_view, kArity>{internal::columnNameOf<std::tuple_element_t<Is, ElemTuple>>()...};
      }(std::make_index_sequence<kArity>{});
  static constexpr std::array<internal::SourceColumns<kArity>, kSourceCount> kSourceColumns =
      []<std::size_t... Ss>(std::index_sequence<Ss...>) {
        std::array<internal::SourceColumns<kArity>, kSourceCount> sources{};
        (
            [&] {
              internal::SourceColumns<kArity>& source = sources.at(Ss);
              source.spread                           = kIsSpread<Ss>;
              for (const std::size_t at : kColumnsOf<Ss>) {
                source.at.at(source.count) = at;
                ++source.count;
              }
            }(),
            ...);
        return sources;
      }(std::make_index_sequence<kSourceCount>{});
  static constexpr std::size_t kVisitTextSize = [] {
    std::size_t size = 0;
    internal::writeVisitOrder(
        kVisitOrder, kSourceColumns, kColumnNames, [&size](std::string_view piece) { size += piece.size(); });
    return size;
  }();
  static constexpr std::array<char, kVisitTextSize> kVisitText = [] {
    std::array<char, kVisitTextSize> text{};
    std::size_t size = 0;
    internal::writeVisitOrder(kVisitOrder, kSourceColumns, kColumnNames, [&](std::string_view piece) {
      for (const char c : piece) {
        text.at(size) = c;
        ++size;
      }
    });
    return text;
  }();

  static_assert(
      [] {
        return [&]<std::size_t... Ss>(std::index_sequence<Ss...>) {
          return (kRefsFromTheirVector<Ss> && ...);
        }(std::make_index_sequence<kSourceCount>{});
      }(),
      DREAMOR_INTERNAL_REFS_FROM_THEIR_VECTOR_MESSAGE);

  // A spread source that is visited after some of its columns are bound is joined on those columns. It is looked up in
  // an index that is built once for each build(), instead of being scanned and compared row by row.

  // The columns that are already bound when each source is visited.
  static constexpr std::array<std::uint64_t, kSourceCount> kBoundBefore = [] {
    const auto produces = []<std::size_t... Ss>(std::index_sequence<Ss...>) {
      return std::array<std::uint64_t, kSourceCount>{kProduces<Ss>...};
    }(std::make_index_sequence<kSourceCount>{});
    std::array<std::uint64_t, kSourceCount> before{};
    std::uint64_t bound = 0;
    for (const std::size_t source : kVisitOrder) {
      if (source >= kSourceCount) {
        break;  // No visit order exists. A chain cannot cause this (planVisitOrder).
      }
      before[source] = bound;
      bound |= produces[source];
    }
    return before;
  }();

  template <std::size_t Source>
  static constexpr bool kJoins = kIsSpread<Source> && (kProduces<Source> & kBoundBefore[Source]) != 0;

  // The key that a joined source is looked up by: its columns that are bound before it, in its own order.
  template <std::size_t Source, typename SourceRow, std::size_t... Is>
  static auto joinKeyOf(std::index_sequence<Is...> /*unused*/) -> decltype(std::tuple_cat(
      std::declval<std::conditional_t<((kBoundBefore[Source] >> kColumnsOf<Source>[Is]) & 1U) != 0,
                                      std::tuple<std::tuple_element_t<Is, SourceRow>>,
                                      std::tuple<>>>()...));

  // A joined source grouped by its key. The CSR rows and the flat index of groupBy are the join index.
  template <std::size_t Source, bool = kJoins<Source>>
  struct JoinIndex {
    using type = std::tuple<>;  // The source is scanned, not looked up.
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
  // Checks the containers and the spread ranges once, as a debug build's check of the contracts.
  template <std::size_t... Ss>
  void checkSources(std::index_sequence<Ss...> /*unused*/) {
    (checkSource<Ss>(), ...);
  }

  template <std::size_t Source>
  void checkSource() {
    using Src = std::tuple_element_t<Source, RangeTuple>;
    // An EntityVector holds each entity once already, so it is not checked again.
    if constexpr (internal::sourceKindOf<Src>() == internal::SourceKind::kContainer &&
                  !internal::IsEntityVectorRange<Src>::value) {
      checks_->template container<typename std::tuple_element_t<kColumnsOf<Source>[0], ElemTuple>::value_type>(
          Source, std::get<Source>(ranges_));
    } else if constexpr (internal::sourceKindOf<Src>() == internal::SourceKind::kSpread) {
      if constexpr (!internal::is_index_set<Src>) { checks_->spread(Source, std::get<Source>(ranges_)); }
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
      if constexpr (internal::is_index_set<std::remove_cvref_t<decltype(source)>>) {
        std::get<Source>(indexes_).emplace(source);  // The index shares the set's rows.
      } else {
        internal::Rows<SourceRow> rows;
        for (const auto& row : source) {
          rows.push_back(row);
        }
        std::get<Source>(indexes_).emplace(internal::SetAccess::make(std::move(rows)));
      }
    }
  }

  // Builds the key of a joined source from the values bound so far.
  template <typename Key, typename BoundCols, typename... Bound>
  [[nodiscard]] static Key boundKey(const Bound&... bound) {
    const auto refs = std::tie(bound...);
    return [&]<typename... Ks>(std::type_identity<std::tuple<Ks...>> /*unused*/) {
      return Key{
          std::get<internal::positionInSequence<internal::indexOfColumn<Ks, ElemTuple>(), BoundCols>()>(refs)...};
    }(std::type_identity<Key>{});
  }

  // Walks the sources in the visit order and binds one column at a time. BoundCols names the element-tuple column of
  // each bound value, in the order in which the values were bound.
  template <std::size_t Step, typename BoundCols, typename... Bound>
  void generate(internal::Rows<ElemTuple>& out, const Bound&... bound) {
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
        // `auto&&` keeps a range returned by value alive for the loop.
        auto&& range = [&]() -> decltype(auto) {
          if constexpr (kKind == internal::SourceKind::kDependent) {
            // The checks of the signature and the columns are at class scope, so they report before any of this code is
            // instantiated.
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
          checks_->template dependent<typename std::tuple_element_t<kCol, ElemTuple>::value_type, decltype(range)>(
              kSource, range);
        }
#endif
        using Next = internal::AppendColumn<BoundCols, kCol>::type;
        for (auto&& element : range) {
          const auto value = internal::toColumn<std::tuple_element_t<kCol, ElemTuple>>(element);
          if (!passes<kCol, Next>(bound..., value)) {
            continue;  // This skips every deeper loop for this partial row.
          }
          generate<Step + 1, Next>(out, bound..., value);
        }
      }
    }
  }

  // A joined source walks only the rows that match the bound values. Any other spread source walks all of its rows.
  template <std::size_t Step, std::size_t Source, typename BoundCols, typename... Bound>
  void visitSpread(internal::Rows<ElemTuple>& out, const Bound&... bound) {
    if constexpr (kJoins<Source>) {
      if (const auto& grouped = std::get<Source>(indexes_)) {  // build() builds the index before generate().
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

  // Unpacks one tuple of a spread source column by column, so that predicates still see every partial row. A column
  // that is already bound is one that the join index matched.
  template <std::size_t Step, std::size_t Source, std::size_t I, typename BoundCols, typename Tup, typename... Bound>
  void spread(internal::Rows<ElemTuple>& out, const Tup& tuple, const Bound&... bound) {
    if constexpr (I == std::tuple_size_v<Tup>) {
      generate<Step + 1, BoundCols>(out, bound...);
    } else {
      constexpr std::size_t kCol = kColumnsOf<Source>[I];
      if constexpr (((internal::maskOfSequence<BoundCols>() >> kCol) & 1U) != 0) {
        spread<Step, Source, I + 1, BoundCols>(out, tuple, bound...);
      } else {
        using Next        = internal::AppendColumn<BoundCols, kCol>::type;
        const auto& value = std::get<I>(tuple);
        if (!passes<kCol, Next>(bound..., value)) { return; }
        spread<Step, Source, I + 1, Next>(out, tuple, bound..., value);
      }
    }
  }

  // Puts the bound values back in declaration order. The visit order is an implementation detail, but the result tuple
  // is not.
  template <typename BoundCols, typename BoundRefs, std::size_t... Is>
  static void emit(internal::Rows<ElemTuple>& out, const BoundRefs& bound, std::index_sequence<Is...> /*unused*/) {
    out.emplace_back(std::get<internal::positionInSequence<Is, BoundCols>()>(bound)...);
  }

  RangeTuple ranges_;
  PredTuple preds_;
  JoinIndexes<std::make_index_sequence<kSourceCount>>::type indexes_;
  internal::SourceChecks* checks_ = nullptr;  // Set only in a debug build, while build() runs.
};

template <typename Builder>
[[nodiscard]] auto applyWhere(Builder builder) {
  return builder;
}

template <typename Builder, typename Pred, typename... Preds>
[[nodiscard]] auto applyWhere(const Builder& builder, Pred pred, Preds... preds) {
  return applyWhere(builder.where(std::move(pred)), std::move(preds)...);
}

// What row_of returned, as a row. A tuple stays as it is, and a single column or value becomes a row of one.
template <typename Returned>
struct ReturnedRow {
  using type = std::tuple<Returned>;
};

template <typename... Ts>
struct ReturnedRow<std::tuple<Ts...>> {
  using type = std::tuple<Ts...>;
};

// How many of the types Ts are T.
template <typename T, typename... Ts>
inline constexpr std::size_t kCountOf =
    (std::size_t{0} + ... + (std::is_same_v<T, Ts> ? std::size_t{1} : std::size_t{0}));

// Which column of the set Cs... is filled by a value R that row_of returned. The column is found as for a key's
// argument, but by type alone, because row_of's values have no order:
//   - A column fills itself.
//   - A column of another name, or a plain value of a column's value type, fills the column of that value type if no
//     other column holds that type.
//   - Otherwise, an integer fills the set's one column of integers, and text fills its one column of text.
// The result is kNoColumn if R fills no column, or if R could fill more than one.
inline constexpr std::size_t kNoColumn = static_cast<std::size_t>(-1);

template <typename R>
struct PlainValueOf {
  using type = R;
};

template <typename R>
  requires column<R>
struct PlainValueOf<R> {
  using type = R::value_type;
};

template <typename R, typename... Cs>
[[nodiscard]] consteval std::size_t columnNamedBy() {
  using Value = PlainValueOf<R>::type;
  std::array<bool, sizeof...(Cs)> fills{};
  if constexpr ((std::same_as<R, Cs> || ...)) {
    fills = {std::same_as<R, Cs>...};
  } else if constexpr (kCountOf<Value, typename Cs::value_type...> != 0) {
    fills = {std::same_as<Value, typename Cs::value_type>...};
  } else if constexpr (!column<R> && comparable_integer<R>) {
    fills = {comparable_integer<typename Cs::value_type>...};
  } else if constexpr (!column<R> && std::is_convertible_v<const R&, std::string_view>) {
    fills = {text_value<typename Cs::value_type>...};
  }
  if (std::ranges::count(fills, true) != 1) { return kNoColumn; }
  return static_cast<std::size_t>(std::ranges::find(fills, true) - fills.begin());
}

// Whether a std::string that row_of returned, held by the row that it returned, would fill a column of
// std::string_view. That column would then view the string after the call ends. A column of std::string copies the
// string.
template <typename R, typename... Cs>
[[nodiscard]] consteval bool viewsReturnedString() {
  if constexpr (std::same_as<R, std::string>) {
    constexpr std::size_t kAt = columnNamedBy<R, Cs...>();
    if constexpr (kAt != kNoColumn) {
      return std::same_as<typename std::tuple_element_t<kAt, std::tuple<Cs...>>::value_type, std::string_view>;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

// How a row that row_of returned fills a row of the set. Each value fills the column that it names (columnNamedBy), and
// each column takes one value.
template <typename Tup, typename Returned>
struct RowFill;

template <typename... Cs, typename... Rs>
struct RowFill<std::tuple<Cs...>, std::tuple<Rs...>> {
  static constexpr std::array<std::size_t, sizeof...(Rs)> kNamedBy{columnNamedBy<std::remove_cvref_t<Rs>, Cs...>()...};
  static constexpr bool kNamed = (column<std::remove_cvref_t<Rs>> && ...);
  // Whether a std::string held by the row that row_of returned fills a column of std::string_view. The string is
  // destroyed with that row after the call.
  static constexpr bool kTextFromCopy = (viewsReturnedString<std::remove_cv_t<Rs>, Cs...>() || ...);
  static constexpr bool kFills        = [] {
    if constexpr (sizeof...(Cs) != sizeof...(Rs)) {
      return false;
    } else {
      std::array<std::size_t, sizeof...(Cs)> taken{};
      for (const std::size_t at : kNamedBy) {
        if (at == kNoColumn) { return false; }
        ++taken.at(at);
      }
      return std::ranges::all_of(taken, [](std::size_t count) { return count == 1; });
    }
  }();

  // The index of the value that fills column C.
  template <std::size_t C>
  static constexpr std::size_t kFrom = static_cast<std::size_t>(std::ranges::find(kNamedBy, C) - kNamedBy.begin());

  [[nodiscard]] static std::tuple<Cs...> make(std::tuple<Rs...>& row) {
    return [&]<std::size_t... C>(std::index_sequence<C...>) {
      return std::tuple<Cs...>{toColumn<Cs>(std::get<kFrom<C>>(row))...};
    }(std::index_sequence_for<Cs...>{});
  }
};

// Whether a set of these columns has a column of std::string_view.
template <typename Tup>
inline constexpr bool kShowsText = false;
template <typename... Cs>
inline constexpr bool kShowsText<std::tuple<Cs...>> = (text_column<Cs> || ...);

// The parameter by which row_of takes a record. It is known only if row_of's operator() is not a template.
template <typename Member>
struct RecordParameter {
  static constexpr bool known = false;
};

template <typename R, typename Class, typename P, typename... Ps>
struct RecordParameter<R (Class::*)(P, Ps...) const> {
  static constexpr bool known = true;
  using type                  = P;
};

template <typename R, typename Class, typename P, typename... Ps>
struct RecordParameter<R (Class::*)(P, Ps...) const noexcept> : RecordParameter<R (Class::*)(P, Ps...) const> {};

// Whether row_of takes each record by value. The copy is destroyed when the call ends.
template <typename RowOf>
[[nodiscard]] consteval bool takesRecordCopy() {
  if constexpr (requires { &RowOf::operator(); }) {
    using Parameter = RecordParameter<decltype(&RowOf::operator())>;
    if constexpr (Parameter::known) {
      return !std::is_reference_v<typename Parameter::type>;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

// The rows of IndexSet{records, row_of}: row_of applied to each record, in the range's order. KeepsRecords says whether
// the records outlive the call. A column of std::string_view needs that, because it views the text where row_of found
// it.
template <typename Tup, bool KeepsRecords, typename Range, typename RowOf>
[[nodiscard]] std::shared_ptr<const SetRows<Tup>> rowsOfRecords(Range& records, const RowOf& row_of) {
  static_assert(KeepsRecords || !kShowsText<Tup>,
                "A text column shows the text where row_of finds it, so the records have to outlive the set: "
                "hand IndexSet{records, row_of} a named container or a view of one - not a temporary, a copy, "
                "nor a range that makes its records.");
  static_assert(!kShowsText<Tup> || !takesRecordCopy<RowOf>(),
                "A text column shows the text where row_of finds it, and row_of takes each record by value, a "
                "copy gone once the call ends: take it by reference - [](const Record& r) - so the text is the "
                "record's own.");
  constexpr bool kRange = std::ranges::input_range<Range>;
  static_assert(kRange,
                "IndexSet{records, row_of} makes a row of each record: give a range of records - a container or a "
                "view.");
  if constexpr (kRange) {
    using Record             = std::ranges::range_reference_t<Range>;
    constexpr bool kCallable = std::invocable<const RowOf&, Record>;
    static_assert(kCallable,
                  "IndexSet{records, row_of}: row_of takes one record and returns its row - a callable over the "
                  "record's type, or `const auto&`.");
    if constexpr (kCallable) {
      using Returned = ReturnedRow<std::remove_cvref_t<std::invoke_result_t<const RowOf&, Record>>>::type;
      using Fill     = RowFill<Tup, Returned>;
      static_assert(!Fill::kTextFromCopy,
                    "IndexSet{records, row_of}: row_of returns a std::string for a column of std::string_view, which "
                    "would show it after the call ends: declare the column over std::string, which holds its text, "
                    "or return a std::string_view on the record's own text.");
      static_assert(!Fill::kNamed || Fill::kFills,
                    "IndexSet{records, row_of}: row_of returns other columns than the set's - return the set's "
                    "columns, in any order: std::tuple<cMachine, cItem>{m, i}.");
      static_assert(Fill::kNamed || Fill::kFills,
                    "IndexSet{records, row_of}: row_of returns plain values, and a value fills the column of its "
                    "type - the one column of that type, or the set's one column of integers or of text - so where "
                    "two columns could take a value, name them: return std::tuple<cFrom, cTo>{a, b}.");
      if constexpr (Fill::kFills && !Fill::kTextFromCopy) {
        Rows<Tup> rows;
        if constexpr (std::ranges::sized_range<Range>) {
          rows.reserve(static_cast<std::size_t>(std::ranges::size(records)));
        }
        for (auto&& record : records) {
          Returned returned{std::invoke(row_of, record)};
          rows.push_back(Fill::make(returned));
        }
#ifndef NDEBUG
        checkRowsOfRecords(rows);
#endif
        return std::make_shared<const SetRows<Tup>>(std::move(rows));
      }
    }
  }
  return std::make_shared<const SetRows<Tup>>(Rows<Tup>{});
}

}  // namespace dreamor::internal::inline index_set

namespace dreamor {

// Runs the same filter as a chain's where() on a set's rows.
template <typename... Columns>
template <typename... Preds>
IndexSet<Columns...> IndexSet<Columns...>::where(Preds... preds) const {
  static_assert(sizeof...(Preds) > 0,
                "where() takes the predicates it applies: S.where(is_open), or several at once - "
                "S.where(fits_batch, is_open).");
  auto source  = std::make_tuple(internal::holdSource(*this));
  using Engine = internal::Set<Tup, decltype(source), std::tuple<>>;
  return internal::applyWhere(Engine{std::move(source), {}}, std::move(preds)...).build();
}

template <typename... Columns>
template <typename Range, typename RowOf>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): the records are only read, one by one
IndexSet<Columns...>::IndexSet(Range&& records, const RowOf& row_of)
    : rows_{internal::rowsOfRecords<std::tuple<Columns...>, internal::keepsElements<Range>()>(records, row_of)} {}

}  // namespace dreamor
