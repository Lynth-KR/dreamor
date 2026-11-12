// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The chain: index_set::relational::from, the steps that add sources and conditions, and build().
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/internal/base/lifetimebound.hpp"
#include "dreamor/internal/index_set/binding.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/element.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/plan.hpp"
#include "dreamor/internal/index_set/set.hpp"

// The message for a step that would add a 65th column.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_TOO_MANY_COLUMNS_MESSAGE                                                                   \
  "Congratulations on a set of more than 64 dimensions - dreamor stops at 64, and even 16 is a lot: split it into " \
  "sets that share key columns, or put a hand on your heart and ask whether the model needs them all."
// The message for a lateral source or a predicate that reads a column that no earlier step binds.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_COLUMNS_BEFORE_MESSAGE                                                                      \
  "lateral and where read only the columns before them in the chain: move this step after the source of the column " \
  "it takes."
// The message for a step or build() called on an lvalue chain that holds a temporary container.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE                                                             \
  "This chain holds a temporary container - from<cPeriod>(periods()) - which is gone once the "             \
  "statement that made it ends: build the set in that statement, or name the container first - const auto " \
  "periodList = periods();"

namespace dreamor::internal::inline index_set {

// What a step whose check failed returns instead of a chain, so that the static_assert message stays the only
// error.
struct StoppedChain {};

// The columns that a chain has so far, in order. It is a bare list of types: in libstdc++, a std::tuple of them would
// instantiate a base class for each column, again at each step of the chain.
template <typename... Columns>
struct ColumnList {};

// The questions that a step asks about the columns that the chain has. Each answer is one fold over the columns, so
// its cost is linear in the number of columns, not quadratic.
template <typename Have>
struct HasColumns;

template <typename... Have>
struct HasColumns<ColumnList<Have...>> {
  static constexpr std::size_t kSize = sizeof...(Have);
  using Tuple                        = std::tuple<Have...>;
  // The list with the columns of Added appended.
  template <typename Added>
  struct With;

  template <typename... Added>
  struct With<ColumnList<Added...>> {
    using type = ColumnList<Have..., Added...>;
  };

  // Whether Column is one of the columns.
  template <typename Column>
  static constexpr bool kHas = (std::is_same_v<Column, Have> || ...);

  // Whether every column in a callable's parameter list is one of the columns.
  template <typename Columns>
  struct HasAll : std::false_type {};

  template <typename... Cs>
  struct HasAll<std::tuple<Cs...>> : std::bool_constant<(kHas<Cs> && ...)> {};

  // The columns of Row that are not among the columns, in the order of Row.
  template <typename Row, typename Lack = ColumnList<>>
  struct Lacking {
    using type = Lack;
  };

  template <typename C, typename... Cs, typename... Lack>
  struct Lacking<std::tuple<C, Cs...>, ColumnList<Lack...>>
      : Lacking<std::tuple<Cs...>, std::conditional_t<kHas<C>, ColumnList<Lack...>, ColumnList<Lack..., C>>> {};
};

// The checks for a step that adds the column Column: from and cross with a range, and lateral with a callable.
template <typename Have, typename Column>
struct NewColumn {
  static constexpr bool kNamed  = column<Column>;
  static constexpr bool kNew    = !kNamed || !HasColumns<Have>::template kHas<Column>;
  static constexpr bool kRoom   = HasColumns<Have>::kSize < kMaxColumns;
  static constexpr bool kPasses = kNamed && kNew && kRoom;

  static consteval void check() {
    static_assert(kNamed,
                  "Name the column: from<cMachine>(machines), not from<Ref<Machine>>(machines). Declare it "
                  "once with `DREAMOR_ENTITY_COLUMN(cMachine, Machine);`.");
    static_assert(kNew,
                  "from<C>, cross<C> and lateral<C> start a new column, and the set has this one already: give "
                  "it a column of its own - `DREAMOR_ENTITY_COLUMN(cTo, Node);` beside cFrom.");
    static_assert(kRoom, DREAMOR_INTERNAL_TOO_MANY_COLUMNS_MESSAGE);
  }
};

// The checks for from and cross with a range of values, which is the same for every row.
template <typename Have, typename Column, typename Range>
struct RangeStep {
  using Source = std::remove_reference_t<Range>;

  static constexpr bool kRange = std::ranges::forward_range<Source&>;
  // If the range holds rows of one column, the values in those rows fill the column (oneColumnRowsFor).
  static constexpr bool kOneColumnRows = kRange && oneColumnRowsFor<Source, Column>();
  static constexpr bool kValues        = !kRange || sourceKindOf<Source>() != SourceKind::kSpread || kOneColumnRows;
  static constexpr bool kOutlive =
      std::is_lvalue_reference_v<Range> || yieldsValues<Source>() || !rowsReferToElements<Source>();
  static constexpr bool kTextOutlives = !kRange || !rowsShowElementText<Source, Column>() || keepsElements<Range>();
  static constexpr bool kPasses = NewColumn<Have, Column>::kPasses && kRange && kValues && kOutlive && kTextOutlives;

  static consteval void check() {
    NewColumn<Have, Column>::check();
    static_assert(kRange,
                  "from<C> and cross<C> take a range, the same for every row: a container, an EntityVector or "
                  "an interval. A list that depends on columns before it is lateral<C>(items_of).");
    static_assert(kValues,
                  "from<C> and cross<C> take a range of values for one column, or the rows of a set of one "
                  "column holding C's value type, and a range of other rows brings its own columns: from(set), "
                  "cross(set) or join(set).");
    static_assert(kOutlive,
                  "An EntityVector handed to from, cross or in must be a named variable, not a temporary: a row "
                  "refers to its elements, so it has to outlive the set. Rows of values, Refs or tuples are "
                  "copied, and a temporary of those is fine.");
    static_assert(kTextOutlives, DREAMOR_INTERNAL_TEXT_OUTLIVES_MESSAGE);
  }
};

// The checks for lateral with a callable, which returns values for each row so far from columns that the chain has.
template <typename Have, typename Column, typename Callable>
struct LateralStep {
  using Source = std::remove_cvref_t<Callable>;

  static constexpr bool kCallable = !std::ranges::forward_range<std::remove_reference_t<Callable>&>;
  static constexpr bool kDirect   = !kCallable || no_indirect_call<Source>;
  static constexpr bool kReadable = !kCallable || !kDirect || ColumnsOfPredicate<Source>::known;
  static constexpr bool kKnown    = kCallable && kDirect && ColumnsOfPredicate<Source>::known;
  static constexpr bool kNamed    = !kKnown || PlacementOf<Source, std::tuple<>>::all_named;
  static constexpr bool kReadOnly = !kKnown || !kNamed || ColumnsOfPredicate<Source>::no_mutable_reference;
  static constexpr bool kBefore =
      !kKnown || !kNamed || HasColumns<Have>::template HasAll<typename ColumnsOfPredicate<Source>::type>::value;
  static constexpr bool kOnce  = !kKnown || PlacementOf<Source, std::tuple<>>::no_repeated_column;
  static constexpr bool kReads = !kKnown || std::tuple_size_v<typename ColumnsOfPredicate<Source>::type> != 0;
  // If Column holds std::string_view, the range that the callable returns must keep its strings after the call.
  static constexpr bool kTextOutlives = [] {
    if constexpr (!kKnown) {
      return true;
    } else {
      using Returned = ReturnTypeOf<const Source&, typename ColumnsOfPredicate<Source>::type>;
      if constexpr (!Returned::invocable) {
        return true;
      } else {
        return !rowsShowElementText<typename Returned::type, Column>() || keepsElements<typename Returned::type>();
      }
    }
  }();

  // The callable must return values, or rows of one column that holds the value type of Column. A function, not a
  // lambda in the initializer: cl.exe instantiates the discarded branches of such a lambda, and range_value_t of a
  // callable returning void then comes before kCallable's message.
  static consteval bool valuesFor() {
    if constexpr (!kKnown) {
      return true;
    } else {
      using Returned = ReturnTypeOf<const Source&, typename ColumnsOfPredicate<Source>::type>;
      if constexpr (!Returned::invocable) {
        return true;
      } else {
        using Range = std::remove_cvref_t<typename Returned::type>;
        if constexpr (!std::ranges::range<Range&>) {
          return true;
        } else {
          return !IsTuple<std::remove_cvref_t<std::ranges::range_value_t<Range&>>>::value ||
                 oneColumnRowsFor<Range, Column>();
        }
      }
    }
  }

  static constexpr bool kValues = valuesFor();
  static constexpr bool kPasses = NewColumn<Have, Column>::kPasses && kCallable && kDirect && kReadable && kNamed &&
                                  kReadOnly && kBefore && kOnce && kReads && kTextOutlives && kValues;

  static consteval void check() {
    NewColumn<Have, Column>::check();
    static_assert(kCallable,
                  "lateral<C> takes a callable over columns before it - [](cMachine m) { return m->items; }; a "
                  "range the same for every row is cross<C>(items).");
    static_assert(kDirect,
                  "lateral<C> cannot take a bare function name or a std::function: either is an indirect call "
                  "per row. Pass a lambda or a function object.");
    static_assert(kReadable,
                  "lateral<C> needs one concrete signature: `[](cMachine m) { ... }`. A generic or `mutable` "
                  "lambda has no readable parameter list - it runs once per row, so it has to be callable as "
                  "const.");
    static_assert(kNamed, "lateral<C> takes the columns themselves: `[](cMachine m)`, not `[](Ref<Machine> m)`.");
    static_assert(kReadOnly, DREAMOR_INTERNAL_READ_ONLY_COLUMNS_MESSAGE);
    static_assert(kBefore, DREAMOR_INTERNAL_COLUMNS_BEFORE_MESSAGE);
    static_assert(kOnce,
                  "lateral<C> takes the same column twice, so both parameters would get one value: give each "
                  "its own column, or take the column once.");
    static_assert(kReads, "lateral<C> reads no column, so its list is the same for every row: cross<C>(items).");
    static_assert(kTextOutlives, DREAMOR_INTERNAL_TEXT_OUTLIVES_MESSAGE);
    static_assert(kValues,
                  "lateral<C> returns values for one column, or the rows of a set of one column holding C's value "
                  "type; rows of more columns are a set to join: join(set).");
  }
};

// The checks for from, cross and join with a set or a range of rows.
enum class RowsStep : std::uint8_t { kCross, kJoin };

template <typename Have, typename Rows, RowsStep Step>
struct SetStep {
  using Source = std::remove_reference_t<Rows>;

  static constexpr bool kRows = std::ranges::forward_range<Source&> && sourceKindOf<Source>() == SourceKind::kSpread;

  template <typename Row>
  struct Shape {
    static constexpr std::size_t kSize   = std::tuple_size_v<Row>;
    static constexpr bool kColumns       = allElementsAreColumns<Row>(std::make_index_sequence<kSize>{});
    static constexpr bool kDistinct      = !kColumns || allColumnsDistinct<Row>(std::make_index_sequence<kSize>{});
    using Added                          = HasColumns<Have>::template Lacking<Row>::type;
    static constexpr std::size_t kAdded  = HasColumns<Added>::kSize;
    static constexpr std::size_t kShared = kSize - kAdded;
    static constexpr bool kRoom          = HasColumns<Have>::kSize + kAdded <= kMaxColumns;
  };

  struct NoShape {
    using Added                          = ColumnList<>;
    static constexpr bool kColumns       = true;
    static constexpr bool kDistinct      = true;
    static constexpr std::size_t kShared = 0;
    static constexpr bool kRoom          = true;
  };

  using Of = std::conditional_t<kRows, Shape<std::ranges::range_value_t<Source>>, NoShape>;

  static constexpr bool kCross  = Step != RowsStep::kCross || Of::kShared == 0;
  static constexpr bool kJoin   = Step != RowsStep::kJoin || Of::kShared != 0;
  static constexpr bool kPasses = kRows && Of::kColumns && Of::kDistinct && Of::kRoom && kCross && kJoin;

  static consteval void check() {
    static_assert(kRows,
                  "from(set), cross(set) and join(set) take a set, or a range of rows of columns; a range of "
                  "values names its column: from<cMachine>(machines), cross<cPeriod>(periods). A table's set is "
                  "its keys(): join(cost.keys()).");
    static_assert(Of::kColumns,
                  "These rows hold values that are not columns, and a set brings its own column types: build the "
                  "rows from declared columns - a set, or a range of std::tuple<cMachine, cItem> - not a "
                  "std::vector<std::tuple<int, int>>.");
    static_assert(Of::kDistinct, "The rows hold one column type twice: give each column its own type.");
    static_assert(Of::kRoom, DREAMOR_INTERNAL_TOO_MANY_COLUMNS_MESSAGE);
    static_assert(kCross,
                  "cross(set) adds new columns, and this set shares a column with the rows so far: to match rows "
                  "on it, join(set).");
    static_assert(kJoin,
                  "join(set) matches rows on the columns it shares with the rows so far, and this set shares "
                  "none: to match on columns of other names, give the set those names first, sBOM.select(as_item) "
                  "returning std::tuple<cItem, cChild>{parent, child}; to pair every row with every row, cross(set).");
  }
};

// The checks for a predicate given to a chain's where(), which may read only columns that the chain has.
template <typename Have, typename Pred>
struct WhereStep {
  static constexpr bool kBefore = !ColumnsOfPredicate<Pred>::known || !PlacementOf<Pred, std::tuple<>>::all_named ||
                                  HasColumns<Have>::template HasAll<typename ColumnsOfPredicate<Pred>::type>::value;

  static consteval void check() {
    checkPredicateOf<Pred>();
    static_assert(kBefore, DREAMOR_INTERNAL_COLUMNS_BEFORE_MESSAGE);
  }
};

// The sources that a chain has so far, the latest last. Each link holds the links before it and one more source, so
// a step adds one small type instead of a longer tuple of every source.
struct NoSources {};

template <typename Before, typename Source>
struct SourceLink {
  Before before;
  Source source;
};

// Returns the source that is Back links before the last one. The first form reads the source from a chain that is
// kept. The second form moves the source out of a chain that build() ends.
template <std::size_t Back, typename Before, typename Source>
[[nodiscard]] const auto& sourceBack(const SourceLink<Before, Source>& link) {
  if constexpr (Back == 0) {
    return link.source;
  } else {
    return sourceBack<Back - 1>(link.before);
  }
}

template <std::size_t Back, typename Before, typename Source>
[[nodiscard]] auto&& sourceBack(SourceLink<Before, Source>&& link) {
  if constexpr (Back == 0) {
    return std::move(link).source;
  } else {
    return sourceBack<Back - 1>(std::move(link).before);
  }
}

// Whether a source passed as Range lasts only until the end of the statement that creates it. A container passed as
// an rvalue does, because the chain holds every container by reference and does not own it. A view is held by value,
// so a view lasts that short only if it views temporaries of the statement (IsStatementScoped).
template <typename Range>
inline constexpr bool kTemporaryContainer =
    !std::is_lvalue_reference_v<Range> &&
    (!std::ranges::view<std::remove_cvref_t<Range>> || IsStatementScoped<std::remove_cvref_t<Range>>::value);

// Whether a source passed as Range is a container passed as an rvalue, which the chain holds by reference. The steps
// that take such a source have a form of their own that DREAMOR_LIFETIMEBOUND marks, so that Clang warns where a
// variable keeps the chain after the container is destroyed. A view is not marked, because the chain holds a view by
// value. Values in braces are marked by their own overloads, which take the std::initializer_list.
template <typename Range>
inline constexpr bool kRvalueContainer =
    !std::is_lvalue_reference_v<Range> && !std::ranges::view<std::remove_cvref_t<Range>>;

// The chain so far. Have lists its columns in the order in which the steps added them; this is the engine's element
// tuple, carried along so that no step computes it again. Sources and Links hold the sources as the engine holds
// them, and Preds holds the predicates. Temporary is true if the chain holds a temporary container.
template <typename Have, typename Sources, typename Links, typename Preds, bool Temporary = false>
class Chain;

// Builds a chain's rows with the columns in another order than the steps added them, for the head of set-builder
// notation (set_builder.hpp). It is a separate struct so that the chain has no such member for a user to see.
struct ChainAccess {
  template <typename Out, typename C>
  [[nodiscard]] static auto build(C&& chain) {
    return std::forward<C>(chain).template buildSet<Out>();
  }

  template <typename Out, typename C>
  [[nodiscard]] static constexpr std::string_view visitOrder() noexcept {
    return std::remove_cvref_t<C>::template EngineFor<Out>::visitOrderText();
  }
};

/// @section Relational algebra
///
/// `index_set::relational::from` and the steps of a chain build a set with the operations of the relational algebra.
/// `from` starts a chain with one source. Each step adds a source or a condition, and `build()` makes the rows. A step
/// reads only the columns that earlier steps add, so a chain reads from top to bottom.
///
/// The chain provides the product (`cross`), the natural join (`join`) and the selection (`where`). The union, the
/// intersection and the difference of two sets are in the Set algebra section. The projection is `IndexSet::select`.
///
/// No rows are made before `build()`. `build()` chooses the visit order, and prefers an order in which conditions run
/// early. It runs each condition as soon as every column that the condition reads is bound. If `build()` visits a set
/// after a source that binds some of the set's columns, it looks up the set's rows by those columns in an index, which
/// it builds once. It does not scan all of the set's rows for each row so far.
///
/// A step holds a container by reference and does not copy it. The rows that `build()` makes hold copies of the
/// values, so a temporary container is permitted if the set is built in the statement that creates the container. A
/// set given to a step shares its rows with the chain, so the set may also be a temporary. Two kinds of source must
/// outlive the set, because the set's rows refer into them:
///
/// - An `EntityVector`: each row holds a `Ref` to one of its entities. A step does not compile if the `EntityVector`
///   is an rvalue.
/// - A range of `std::string`s for a column of `std::string_view`: the column's values are views of the text inside
///   the strings. A step does not compile if the range is a temporary container or makes its strings as it is read.
///
/// If the set is used after such a source is destroyed, the behavior is undefined. So is building a chain over a
/// temporary container after the statement that creates the container, which the `Chain` entry describes, with the
/// warning that Clang gives.
///
/// Guide: guide/relational-algebra.md

/// @exposed
/// A set under construction: the sources and conditions given to `index_set::relational::from` and to the steps after
/// it. No rows are made until `build()`. The type is internal. Hold a chain in an `auto` variable.
///
/// Each step returns a new chain. Called on an rvalue, such as the result of the previous step, a step moves the
/// sources and conditions into the new chain. Called on an lvalue, it copies the sources and conditions, and the
/// original chain remains usable. `build()` does the same. A container passed as a source is held by reference, so
/// copying a chain does not copy the container's elements.
///
/// A step does not compile in these cases:
///
/// - It adds a column that the chain already has.
/// - It reads a column that no earlier step adds.
/// - It would give the chain a 65th column.
///
/// A step or `build()` called on an lvalue chain that holds a temporary container does not compile. A temporary
/// container lasts only until the end of the statement that creates it, so the set must be built in that statement.
/// No type can stop `std::move` of such a chain: the step or `build()` then compiles, and the behavior is undefined.
/// A chain has no need of `std::move`. A chain over containers that outlive it is used as an lvalue, and a chain over
/// a temporary container is built in the statement that creates the container.
///
/// With Clang, the steps that take a container as an rvalue, and the values written in braces, are marked with
/// Clang's `lifetimebound` attribute. Clang then warns (`-Wdangling`, on by default) at the declaration of a variable
/// that keeps a chain over a temporary container, before the chain is used. GCC and MSVC give no such warning.
template <typename Have, typename... Sources, typename Links, typename... Preds, bool Temporary>
class Chain<Have, ColumnList<Sources...>, Links, std::tuple<Preds...>, Temporary> {
  using Brought = HasColumns<Have>::Tuple;
  // The engine that makes rows with the columns in the order of Out. The chain's own order is the order in which the
  // steps added the columns.
  template <typename Out>
  using EngineFor = Set<Out, std::tuple<Sources...>, std::tuple<Preds...>, Brought>;
  using Engine    = EngineFor<Brought>;

 public:
  /// @hidden - made by from and the steps
  Chain(Links links, std::tuple<Preds...> preds) : links_{std::move(links)}, preds_{std::move(preds)} {}

  /// @anchor chain-cross-column
  /// Returns a new chain that adds the column `Column` and pairs every row so far with every element of `range`.
  ///
  /// `range` is the same for every row so far. For a list that depends on the row, use `lateral`.
  ///
  /// Takes: `Column` must be a column that the chain does not have. `range` must be a source that
  /// `index_set::relational::from` takes for the column `Column`. `range` may be a temporary container, but not a
  /// temporary `EntityVector`.
  /// Contract: column_integer_in_range
  template <typename Column, typename Range>
    requires(!kRvalueContainer<Range>)
  [[nodiscard]] auto cross(Range&& range) && {
    return crossOf<Column>(std::move(*this), std::forward<Range>(range));
  }

  /// @hidden - the same step for a container passed as an rvalue, marked for Clang's lifetime warning
  template <typename Column, typename Range>
    requires kRvalueContainer<Range>
  [[nodiscard]] auto cross(Range&& range DREAMOR_LIFETIMEBOUND) && {
    return crossOf<Column>(std::move(*this), std::forward<Range>(range));
  }

  /// @overload
  template <typename Column, typename Range>
    requires(!kRvalueContainer<Range>)
  [[nodiscard]] auto cross(Range&& range) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return crossOf<Column>(*this, std::forward<Range>(range));
  }

  /// @hidden - the same step for a container passed as an rvalue, marked for Clang's lifetime warning
  template <typename Column, typename Range>
    requires kRvalueContainer<Range>
  [[nodiscard]] auto cross(Range&& range DREAMOR_LIFETIMEBOUND) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return crossOf<Column>(*this, std::forward<Range>(range));
  }

  /// @overload
  /// Returns a new chain that adds the column `Column` and pairs every row so far with each value written in braces,
  /// in the order written.
  ///
  /// The values are a temporary container: they last until the end of the statement that writes them. A step or
  /// `build()` called on an lvalue chain that holds them does not compile, so the set must be built in that statement.
  ///
  /// Takes: `Column` must be a column that the chain does not have. Each value must be of the value type of `Column`,
  /// or of a type that converts to it. Values of any integer type may fill a column of integers, and floating-point
  /// values do not fill a column of integers. For an entity column, the values must be `Ref`s, or columns or rows of
  /// one column that hold `Ref`s. For a column of `std::string_view`, the values must not be `std::string`s.
  /// Contract: column_integer_in_range
  template <typename Column, typename Value>
  [[nodiscard]] auto cross(std::initializer_list<Value> values DREAMOR_LIFETIMEBOUND) && {
    return crossOf<Column>(std::move(*this), BracedValues<Value>{values.begin(), values.end()});
  }

  /// @overload
  template <typename Column, typename Value>
  [[nodiscard]] auto cross(std::initializer_list<Value> values DREAMOR_LIFETIMEBOUND) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return crossOf<Column>(*this, BracedValues<Value>{values.begin(), values.end()});
  }

  /// @anchor chain-cross-rows
  /// Returns a new chain that adds the columns of `rows` and pairs every row so far with every row of `rows`.
  ///
  /// To match rows on a shared column, use `join`.
  ///
  /// Takes: `rows` must be a set or a range of rows. `rows` must share no column with the chain. `rows` may be a
  /// temporary.
  template <typename Rows>
    requires(!column<Rows> && !kRvalueContainer<Rows>)
  [[nodiscard]] auto cross(Rows&& rows) && {
    return rowsOf<RowsStep::kCross>(std::move(*this), std::forward<Rows>(rows));
  }

  /// @hidden - the same step for a container passed as an rvalue, marked for Clang's lifetime warning
  template <typename Rows>
    requires(!column<Rows> && kRvalueContainer<Rows>)
  [[nodiscard]] auto cross(Rows&& rows DREAMOR_LIFETIMEBOUND) && {
    return rowsOf<RowsStep::kCross>(std::move(*this), std::forward<Rows>(rows));
  }

  /// @overload
  template <typename Rows>
    requires(!column<Rows> && !kRvalueContainer<Rows>)
  [[nodiscard]] auto cross(Rows&& rows) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return rowsOf<RowsStep::kCross>(*this, std::forward<Rows>(rows));
  }

  /// @hidden - the same step for a container passed as an rvalue, marked for Clang's lifetime warning
  template <typename Rows>
    requires(!column<Rows> && kRvalueContainer<Rows>)
  [[nodiscard]] auto cross(Rows&& rows DREAMOR_LIFETIMEBOUND) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return rowsOf<RowsStep::kCross>(*this, std::forward<Rows>(rows));
  }

  /// Returns a new chain that matches the rows so far with the rows of `rows` on the columns they share.
  ///
  /// This is the natural join. A row so far and a row of `rows` combine into one row if their values are equal in
  /// every shared column. The new chain adds the columns of `rows` that the chain does not have. If the chain already
  /// has every column of `rows`, the join adds no column and keeps only the rows so far that are in `rows`.
  ///
  /// To match on columns that have different names, first rename the columns of `rows` with `IndexSet::select`.
  ///
  /// Takes: `rows` must be a set or a range of rows. `rows` must share at least one column with the chain. `rows` may
  /// be a temporary.
  /// Cost: `build()` matches the rows through an index on the shared columns, which it builds once. It does not compare
  /// every pair of rows.
  template <typename Rows>
    requires(!kRvalueContainer<Rows>)
  [[nodiscard]] auto join(Rows&& rows) && {
    return rowsOf<RowsStep::kJoin>(std::move(*this), std::forward<Rows>(rows));
  }

  /// @hidden - the same step for a container passed as an rvalue, marked for Clang's lifetime warning
  template <typename Rows>
    requires kRvalueContainer<Rows>
  [[nodiscard]] auto join(Rows&& rows DREAMOR_LIFETIMEBOUND) && {
    return rowsOf<RowsStep::kJoin>(std::move(*this), std::forward<Rows>(rows));
  }

  /// @overload
  template <typename Rows>
    requires(!kRvalueContainer<Rows>)
  [[nodiscard]] auto join(Rows&& rows) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return rowsOf<RowsStep::kJoin>(*this, std::forward<Rows>(rows));
  }

  /// @hidden - the same step for a container passed as an rvalue, marked for Clang's lifetime warning
  template <typename Rows>
    requires kRvalueContainer<Rows>
  [[nodiscard]] auto join(Rows&& rows DREAMOR_LIFETIMEBOUND) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return rowsOf<RowsStep::kJoin>(*this, std::forward<Rows>(rows));
  }

  /// Returns a new chain that adds the column `Column`, with the values that `callable` returns for each row so far.
  ///
  /// For each row so far, `build()` calls `callable` with the columns that it reads. Each element of the returned
  /// range makes one new row: the row so far, with that element in `Column`. `callable` returns one of these, by the
  /// kind of `Column`:
  ///
  /// - For an entity column: a range of `Ref`s to entities of a frozen `EntityVector`, or a reference to that
  ///   `EntityVector`.
  /// - For a value column: a range of values that fill the column as in `index_set::relational::from`, an interval,
  ///   or a set of one column that holds the column's value type.
  /// - For a column of `std::string_view`, if the range holds `std::string`s: a reference to the range, or a borrowed
  ///   view of it such as `std::span`.
  ///
  /// If the set is used after the entities or the strings that the returned ranges refer to are destroyed, the
  /// behavior is undefined.
  ///
  /// Takes: `Column` must be a column that the chain does not have. `callable` must be a column callable that reads at
  /// least one column. An earlier step must add every column that `callable` reads. `callable` is copied or moved
  /// into the chain, so it may be a temporary.
  /// Contract: column_integer_in_range
  template <typename Column, typename Callable>
  [[nodiscard]] auto lateral(Callable&& callable) && {
    return lateralOf<Column>(std::move(*this), std::forward<Callable>(callable));
  }

  /// @overload
  template <typename Column, typename Callable>
  [[nodiscard]] auto lateral(Callable&& callable) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return lateralOf<Column>(*this, std::forward<Callable>(callable));
  }

  /// Returns a new chain that keeps only the rows for which every condition in `preds` is true.
  ///
  /// The conditions of several calls to `where` combine with AND. `build()` runs each condition as soon as every
  /// column that it reads is bound, so the place of a `where` in the chain does not change the rows.
  ///
  /// Takes: `preds` must be one or more conditions. An earlier step must add every column that a condition reads.
  template <typename... Added>
  [[nodiscard]] auto where(Added... preds) && {
    return whereOf(std::move(*this), std::move(preds)...);
  }

  /// @overload
  template <typename... Added>
  [[nodiscard]] auto where(Added... preds) const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return whereOf(*this, std::move(preds)...);
  }

  /// Makes the rows of the set that the chain describes.
  ///
  /// The rows come in the order of nested loops over the sources, in the visit order: the source visited first varies
  /// slowest. `visitOrder()` returns the visit order. The visit order need not be the order of the steps.
  ///
  /// Returns: An `IndexSet` whose columns are in the order in which the steps added them.
  /// Contract: source_holds_no_repeat, source_holds_no_empty_ref
  [[nodiscard]] auto build() && { return std::move(*this).template buildSet<Brought>(); }

  /// @overload
  [[nodiscard]] auto build() const& {
    static_assert(!Temporary, DREAMOR_INTERNAL_CHAIN_OUTLIVES_MESSAGE);
    return buildSet<Brought>();
  }

  /// Returns the visit order of `build()`, as the names of the sources' columns.
  ///
  /// The function is `static` and `constexpr`, so the visit order is known at compile time.
  ///
  /// Returns: A string in which the sources are separated by commas, and the columns of a set or of a range of rows
  /// are in braces, for example `cMachine, {cItem, cPeriod}`.
  /// Cost: The string is made at compile time, and only for a chain whose `visitOrder()` is called.
  [[nodiscard]] static constexpr std::string_view visitOrder() noexcept { return Engine::visitOrderText(); }

 private:
  // Each step is written once for both forms: Self is the chain, moved from an rvalue or copied from an lvalue.
  template <typename Column, typename Self, typename Range>
  [[nodiscard]] static auto crossOf(Self&& self, Range&& range) {
    using Step = RangeStep<Have, Column, Range>;
    Step::check();
    if constexpr (Step::kPasses && Step::kOneColumnRows) {
      return std::forward<Self>(self).template add<ColumnList<Column>, kTemporaryContainer<Range>>(
          std::views::transform(holdSource(std::forward<Range>(range)), FirstColumn{}));
    } else if constexpr (Step::kPasses) {
      return std::forward<Self>(self).template add<ColumnList<Column>, kTemporaryContainer<Range>>(
          holdSource(std::forward<Range>(range)));
    } else {
      return StoppedChain{};
    }
  }

  template <RowsStep Kind, typename Self, typename Rows>
  [[nodiscard]] static auto rowsOf(Self&& self, Rows&& rows) {
    using Step = SetStep<Have, Rows, Kind>;
    Step::check();
    if constexpr (Step::kPasses) {
      return std::forward<Self>(self).template add<typename Step::Of::Added, kTemporaryContainer<Rows>>(
          holdSource(std::forward<Rows>(rows)));
    } else {
      return StoppedChain{};
    }
  }

  template <typename Column, typename Self, typename Callable>
  [[nodiscard]] static auto lateralOf(Self&& self, Callable&& callable) {
    using Step = LateralStep<Have, Column, Callable>;
    Step::check();
    if constexpr (Step::kPasses) {
      return std::forward<Self>(self).template add<ColumnList<Column>, false>(
          std::remove_cvref_t<Callable>{std::forward<Callable>(callable)});
    } else {
      return StoppedChain{};
    }
  }

  template <typename... Added>
  using WhereLonger = Chain<Have, ColumnList<Sources...>, Links, std::tuple<Preds..., Added...>, Temporary>;

  template <typename... Added>
  static void checkWhere() {
    static_assert(sizeof...(Added) > 0,
                  "where() takes the predicates it applies: .where(is_open), or several at once - "
                  ".where(fits_batch, is_open).");
    (WhereStep<Have, Added>::check(), ...);
  }

  // self is taken by value: it is moved from an rvalue chain and copied from an lvalue chain.
  template <typename... Added>
  [[nodiscard]] static auto whereOf(Chain self, Added... preds) {
    checkWhere<Added...>();
    return WhereLonger<Added...>{
        std::move(self.links_),
        std::tuple_cat(std::move(self.preds_), std::make_tuple(std::move(preds)...)),
    };
  }

  // The engine takes the sources and predicates by value. The const& form copies them from a chain that is kept for
  // more steps. The && form moves them out of a chain that ends here, so that a membership's index or a callable's
  // captures are not copied again.
  template <typename Out>
  [[nodiscard]] auto buildSet() const& {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      return EngineFor<Out>{std::tuple<Sources...>{sourceBack<sizeof...(Sources) - 1 - Is>(links_)...}, preds_}.build();
    }(std::index_sequence_for<Sources...>{});
  }

  template <typename Out>
  [[nodiscard]] auto buildSet() && {
    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      // Each call moves the source out of its own link. No call moves from links_ as a whole.
      return EngineFor<Out>{
          std::tuple<Sources...>{sourceBack<sizeof...(Sources) - 1 - Is>(std::move(links_))...},
          std::move(preds_),
      }
          .build();
    }(std::index_sequence_for<Sources...>{});
  }

  template <typename Added, bool AddsTemporary, typename Source>
  [[nodiscard]] auto add(Source source) && {
    using Longer = SourceLink<Links, Source>;
    return Chain<typename HasColumns<Have>::template With<Added>::type,
                 ColumnList<Sources..., Source>,
                 Longer,
                 std::tuple<Preds...>,
                 Temporary || AddsTemporary>{Longer{std::move(links_), std::move(source)}, std::move(preds_)};
  }

  template <typename Added, bool AddsTemporary, typename Source>
  [[nodiscard]] auto add(Source source) const& {
    using Longer = SourceLink<Links, Source>;
    return Chain<typename HasColumns<Have>::template With<Added>::type,
                 ColumnList<Sources..., Source>,
                 Longer,
                 std::tuple<Preds...>,
                 Temporary || AddsTemporary>{Longer{links_, std::move(source)}, preds_};
  }

  template <typename, typename, typename, typename, bool>
  friend class Chain;
  friend struct ChainAccess;

  Links links_;
  std::tuple<Preds...> preds_;
};

using EmptyChain = Chain<ColumnList<>, ColumnList<>, NoSources, std::tuple<>>;

}  // namespace dreamor::internal::inline index_set

// from is the only free function of a chain. It applies a step to the empty chain, which stands for the relation
// with no column and one row (TABLE_DEE): a step applied to that relation returns what from returns.
namespace dreamor::index_set::relational {

/// @anchor from-column
/// Returns a chain with the column `Column` and one row for each element of `range`.
///
/// `range` must be one of these, by the kind of `Column`:
///
/// - For an entity column: the frozen `EntityVector` of the column's entity type, as an lvalue, or a range of `Ref`s
///   to its entities.
/// - For a value column: a range of values of the column's value type, or of a type that converts to it. Values of any
///   integer type fill a column of integers, and `std::string_view`s fill a column of `std::string`. Floating-point
///   values do not fill a column of integers.
/// - For a column of integers: an interval.
/// - For any column: a set of one column, or a range of rows of one column, whose column holds the value type of
///   `Column`.
///
/// If `range` is a temporary container, a step or `build()` called on an lvalue chain that holds it does not compile,
/// so the set must be built in the statement that creates `range`. If the set is used after an `EntityVector` or the
/// strings that its rows refer to are destroyed, the behavior is undefined.
///
/// Takes: `Column` must be a column. `range` must be one of the sources listed above. If `Column` holds
/// `std::string_view` and `range` holds `std::string`s, `range` must be an lvalue or a borrowed view. `range` may be a
/// temporary container, but not a temporary `EntityVector`.
/// Contract: column_integer_in_range
template <typename Column, typename Range>
  requires(!internal::kRvalueContainer<Range>)
[[nodiscard]] auto from(Range&& range) {
  return internal::EmptyChain{{}, {}}.template cross<Column>(std::forward<Range>(range));
}

/// @hidden - the same function for a container passed as an rvalue, marked for Clang's lifetime warning
template <typename Column, typename Range>
  requires internal::kRvalueContainer<Range>
[[nodiscard]] auto from(Range&& range DREAMOR_LIFETIMEBOUND) {
  return internal::EmptyChain{{}, {}}.template cross<Column>(std::forward<Range>(range));
}

/// @overload
/// Returns a chain with the column `Column` and one row for each value written in braces, in the order written.
///
/// The values are a temporary container: they last until the end of the statement that writes them. A step or
/// `build()` called on an lvalue chain that holds them does not compile, so the set must be built in that statement.
///
/// Takes: `Column` must be a column. Each value must be of the value type of `Column`, or of a type that converts to
/// it. Values of any integer type may fill a column of integers, and floating-point values do not fill a column of
/// integers. For an entity column, the values must be `Ref`s, or columns or rows of one column that hold `Ref`s. For a
/// column of `std::string_view`, the values must not be `std::string`s.
/// Contract: column_integer_in_range
template <typename Column, typename Value>
[[nodiscard]] auto from(std::initializer_list<Value> values DREAMOR_LIFETIMEBOUND) {
  return internal::EmptyChain{{}, {}}.template cross<Column>(
      internal::BracedValues<Value>{values.begin(), values.end()});
}

/// @hidden - exists only to reject the call with a message: from takes one column
template <typename First, typename Second, typename... More, typename Range>
  requires internal::column<First> && internal::column<Second>
auto from(Range&& /*range*/) {
  static_assert(internal::always_false<First>,
                "from<C> names one column. Rows of several columns from data rows are "
                "IndexSet<cPlant, cCustomer>{lanes, lane_of}, lane_of returning a row of each; a product of "
                "sources is a chain, from<cPlant>(plants).cross<cCustomer>(customers).");
  return internal::EmptyChain{{}, {}};
}

/// @anchor from-rows
/// Returns a chain with the columns and the rows of `rows`.
///
/// If `rows` is a temporary container, a step or `build()` called on an lvalue chain that holds it does not compile,
/// so the set must be built in the statement that creates `rows`.
///
/// Takes: `rows` must be a set or a range of rows. `rows` may be a temporary.
template <typename Rows>
  requires(!internal::column<Rows> && !internal::kRvalueContainer<Rows>)
[[nodiscard]] auto from(Rows&& rows) {
  return internal::EmptyChain{{}, {}}.cross(std::forward<Rows>(rows));
}

/// @hidden - the same function for a container passed as an rvalue, marked for Clang's lifetime warning
template <typename Rows>
  requires(!internal::column<Rows> && internal::kRvalueContainer<Rows>)
[[nodiscard]] auto from(Rows&& rows DREAMOR_LIFETIMEBOUND) {
  return internal::EmptyChain{{}, {}}.cross(std::forward<Rows>(rows));
}

}  // namespace dreamor::index_set::relational
