// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Set-builder notation: of and in, lowered onto the chain of relational.hpp, and SetOf, the set that of() writes
// until build().
#pragma once

#include <algorithm>
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
#include "dreamor/internal/index_set/relational.hpp"

// The message for where() or build() called on an lvalue SetOf that holds a temporary container.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_SET_BUILDER_OUTLIVES_MESSAGE                                                            \
  "This set reads a temporary container - in<cPeriod>(periods()) - which is gone once the statement that wrote " \
  "it ends: build the set in that statement, or name the container first - const auto periodList = periods();"

namespace dreamor::internal::inline index_set {

// A generator: the columns that it binds, and its source as passed to in(). The source is held as a reference in a
// std::tuple, as std::forward_as_tuple holds its arguments, so it lasts only for the statement. Values in braces are
// held by value.
template <typename Binds, typename Source>
struct Generator {
  std::tuple<Source> source;
};

// Whether a generator's source lasts only until the end of the statement that creates it, as a temporary container
// and values in braces do. A set written with such a generator must be built in that statement. Only a range can be
// one: a column callable passed as an rvalue is moved into the chain, which then owns it.
template <typename G>
inline constexpr bool kTemporaryGenerator = false;
template <typename Binds, typename Source>
inline constexpr bool kTemporaryGenerator<Generator<Binds, Source>> =
    std::ranges::range<std::remove_cvref_t<Source>> && kTemporaryContainer<Source>;

// The columns that a chain has bound so far, and whether the chain holds a temporary container.
template <typename C>
struct ChainShape;

template <typename Have, typename Sources, typename Links, typename Preds, bool Temporary>
struct ChainShape<Chain<Have, Sources, Links, Preds, Temporary>> {
  using Bound                      = Have;
  static constexpr bool kTemporary = Temporary;
};

template <typename List>
struct FirstOfColumns;

template <typename C, typename... Cs>
struct FirstOfColumns<ColumnList<C, Cs...>> {
  using type = C;
};

// SharesColumn: whether a list of columns has a column of another list. SameColumns: whether two lists hold the same
// columns, each once.
template <typename Have, typename Of>
struct SharesColumn;

template <typename Have, typename... Cs>
struct SharesColumn<Have, ColumnList<Cs...>> : std::bool_constant<(HasColumns<Have>::template kHas<Cs> || ...)> {};

template <typename A, typename B>
struct SameColumns : std::false_type {};

template <typename... As, typename... Bs>
struct SameColumns<ColumnList<As...>, ColumnList<Bs...>>
    : std::bool_constant<sizeof...(As) == sizeof...(Bs) && (HasColumns<ColumnList<Bs...>>::template kHas<As> && ...) &&
                         (HasColumns<ColumnList<As...>>::template kHas<Bs> && ...)> {};
template <typename Row>
struct RowColumns;

template <typename... Cs>
struct RowColumns<std::tuple<Cs...>> {
  using type = ColumnList<Cs...>;
};

// The kind of source that a generator passes, decided by its type alone.
enum class GeneratorKind : std::uint8_t { kSet, kValues, kCallable, kNeither };

template <typename Binds, typename Source>
[[nodiscard]] consteval GeneratorKind generatorKindOf() {
  using Bare = std::remove_cvref_t<Source>;
  if constexpr (std::ranges::forward_range<Bare&>) {
    if constexpr (sourceKindOf<Bare>() == SourceKind::kSpread) {
      // A set of one column whose column is not the generator's column counts as a range of values.
      using Row = std::ranges::range_value_t<Bare&>;
      if constexpr (HasColumns<Binds>::kSize == 1 && OneColumnRow<Row>::value) {
        if constexpr (!std::is_same_v<typename OneColumnRow<Row>::column_type, typename FirstOfColumns<Binds>::type>) {
          return GeneratorKind::kValues;
        } else {
          return GeneratorKind::kSet;
        }
      } else {
        return GeneratorKind::kSet;
      }
    } else {
      return GeneratorKind::kValues;
    }
  } else if constexpr (std::is_class_v<Bare> || std::is_pointer_v<Bare> || std::is_function_v<Bare>) {
    return GeneratorKind::kCallable;
  } else {
    return GeneratorKind::kNeither;
  }
}

// The checks of GeneratorCheck that read a type only one kind of source has. They are function templates, not
// lambdas called in the member initializers: cl.exe instantiates the discarded branch of an if constexpr inside such a
// lambda, and fails on the row type of a source that has none.
template <GeneratorKind Kind, typename Binds, typename Bare>
[[nodiscard]] consteval bool ownColumns() {
  if constexpr (Kind == GeneratorKind::kSet) {
    return SameColumns<Binds, typename RowColumns<std::ranges::range_value_t<Bare&>>::type>::value;
  } else {
    return true;
  }
}

template <GeneratorKind Kind, std::size_t Count, typename Binds, typename Bare>
[[nodiscard]] consteval bool sameValues() {
  if constexpr (Kind == GeneratorKind::kValues && Count == 1 && sourceKindOf<Bare>() == SourceKind::kSpread) {
    using Row = std::ranges::range_value_t<Bare&>;
    return std::is_same_v<typename OneColumnRow<Row>::column_type::value_type,
                          typename FirstOfColumns<Binds>::type::value_type>;
  } else {
    return true;
  }
}

template <bool Known, typename Bound, typename Bare>
[[nodiscard]] consteval bool boundFirst() {
  if constexpr (Known) {
    return HasColumns<Bound>::template HasAll<typename ColumnsOfPredicate<Bare>::type>::value;
  } else {
    return true;
  }
}

// The checks for a generator against the columns bound before it, with messages in this notation's terms. kPasses
// says whether to lower the generator.
template <typename Bound, typename Binds, typename Source>
struct GeneratorCheck {
  using Bare                           = std::remove_cvref_t<Source>;
  static constexpr GeneratorKind kKind = generatorKindOf<Binds, Source>();
  static constexpr std::size_t kCount  = HasColumns<Binds>::kSize;

  static constexpr bool kSource     = kKind != GeneratorKind::kNeither;
  static constexpr bool kOwnColumns = ownColumns<kKind, Binds, Bare>();
  static constexpr bool kOneColumn  = kKind == GeneratorKind::kSet || kKind == GeneratorKind::kNeither || kCount == 1;
  static constexpr bool kSameValues = sameValues<kKind, kCount, Binds, Bare>();
  static constexpr bool kDirect     = kKind != GeneratorKind::kCallable || no_indirect_call<Bare>;
  static constexpr bool kReadable   = kKind != GeneratorKind::kCallable || !kDirect || ColumnsOfPredicate<Bare>::known;
  static constexpr bool kKnown      = kKind == GeneratorKind::kCallable && kDirect && ColumnsOfPredicate<Bare>::known;
  static constexpr bool kBoundFirst = boundFirst<kKnown, Bound, Bare>();
  static constexpr bool kReads      = !kKnown || std::tuple_size_v<typename ColumnsOfPredicate<Bare>::type> != 0;
  static constexpr bool kPasses =
      kSource && kOwnColumns && kOneColumn && kSameValues && kDirect && kReadable && kBoundFirst && kReads;

  static consteval void check() {
    static_assert(kSource,
                  "in<C>(source) takes a set, a range of values or a callable over the columns bound before it: "
                  "in<cMachine>(machines), in<cMachine, cItem>(sROUTES), in<cItem>(items_of).");
    static_assert(kOwnColumns,
                  "in<C...>(set) names the set's own columns, each of them and no other, in any order - "
                  "in<cMachine, cItem>(sROUTES): a column of another name is bound by its own generator, or the set "
                  "renamed first, sBOM.select(as_item).");
    static_assert(kOneColumn,
                  "in<C>(values) and in<C>(f) bind one column each - in<cMachine>(machines); columns bound together "
                  "come from a set of them: in<cMachine, cItem>(sROUTES).");
    static_assert(kSameValues,
                  "A set of one column fills a column of another name only when both hold the same type: "
                  "in<cLater>(sPERIODS) takes a cLater of the type cPeriod holds.");
    static_assert(kDirect,
                  "in<C>(f) cannot take a bare function name or a std::function: either is an indirect call per row. "
                  "Pass a lambda or a function object.");
    static_assert(kReadable,
                  "in<C>(f) needs f's columns, written out - `[](cMachine m) { ... }`: a generic or `mutable` lambda "
                  "has no parameter list to read.");
    static_assert(kBoundFirst,
                  "in<C>(f) reads a column no generator before it binds: put that column's generator first - "
                  "in<cMachine>(machines) before in<cItem>(items_of).");
    static_assert(kReads,
                  "in<C>(f) with f reading no column is the same list for every row: hand the list itself, "
                  "in<cItem>(items).");
  }
};

// The condition for a generator whose column C is already bound and whose source is a column callable. It keeps a
// row if the row's value in C is among the values that the callable returns for the columns it reads. The callable
// returns what a lateral source returns: values, an interval, or a set of one column, of C or of another column of
// the same value type. An interval's value is compared as an integer, so a value out of C's range is never among them.
template <typename C, typename F, typename Reads>
class AmongListOf;

template <typename C, typename F, typename... Rs>
class AmongListOf<C, F, std::tuple<Rs...>> {
 public:
  explicit AmongListOf(F list_of) : list_of_{std::move(list_of)} {}

  [[nodiscard]] bool operator()(Rs... reads, C value) const {
    const auto is_value = [&value](const auto& element) {
      using Element = std::remove_cvref_t<decltype(element)>;
      if constexpr (OneColumnRow<Element>::value) {
        return value.get() == std::get<0>(element).get();
      } else if constexpr (IsIntervalPoint<Element>::value) {
        static_assert(comparable_integer<typename C::value_type> && IsIntervalPoint<Element>::value,
                      "An interval fills a column of integers: DREAMOR_VALUE_COLUMN(cPeriod, int).");
        return std::cmp_equal(value.get(), element.value);
      } else {
        return value == element;
      }
    };
    return std::ranges::any_of(list_of_(reads...), is_value);
  }

 private:
  F list_of_;
};

// A range of values as rows of the one column C. If C is already bound, the engine looks the rows up as a join on C,
// which acts as a semijoin. A temporary container is read through a StatementView, so that the chain counts it as a
// temporary container, as it counts any other.
template <typename C, typename Source>
[[nodiscard]] auto rowsOfValues(Source&& source) {
  using Bare = std::remove_cvref_t<Source>;
  if constexpr (std::is_lvalue_reference_v<Source> || (std::ranges::view<Bare> && !IsStatementScoped<Bare>::value)) {
    return namedAsOneColumn<C>(std::forward<Source>(source));
  } else {
    return StatementView{namedAsOneColumn<C>(std::ranges::ref_view{source})};
  }
}

// Lowers one generator onto the chain so far.
template <typename C, typename Binds, typename Source>
[[nodiscard]] auto lowerGenerator(C&& chain, Generator<Binds, Source>&& generator) {
  using Bound = ChainShape<std::remove_cvref_t<C>>::Bound;
  using Check = GeneratorCheck<Bound, Binds, Source>;
  Check::check();
  if constexpr (!Check::kPasses) {
    return StoppedChain{};
  } else if constexpr (Check::kKind == GeneratorKind::kSet) {
    if constexpr (SharesColumn<Bound, Binds>::value) {
      return std::forward<C>(chain).join(std::get<0>(std::move(generator).source));
    } else {
      return std::forward<C>(chain).cross(std::get<0>(std::move(generator).source));
    }
  } else {
    using Column       = FirstOfColumns<Binds>::type;
    constexpr bool kOn = HasColumns<Bound>::template kHas<Column>;
    if constexpr (Check::kKind == GeneratorKind::kValues && kOn) {
      return std::forward<C>(chain).join(rowsOfValues<Column>(std::get<0>(std::move(generator).source)));
    } else if constexpr (Check::kKind == GeneratorKind::kValues) {
      return std::forward<C>(chain).template cross<Column>(std::get<0>(std::move(generator).source));
    } else if constexpr (kOn) {
      using Bare = std::remove_cvref_t<Source>;
      return std::forward<C>(chain).where(
          AmongListOf<Column, Bare, typename ColumnsOfPredicate<Bare>::type>{std::get<0>(std::move(generator).source)});
    } else {
      return std::forward<C>(chain).template lateral<Column>(std::get<0>(std::move(generator).source));
    }
  }
}

template <typename C>
[[nodiscard]] auto lowerGenerators(C&& chain) {
  return std::forward<C>(chain);
}

template <typename C, typename G, typename... Gs>
[[nodiscard]] auto lowerGenerators(C&& chain, G&& generator, Gs&&... more) {
  if constexpr (std::is_same_v<std::remove_cvref_t<C>, StoppedChain>) {
    return StoppedChain{};  // Only the first mistake is reported.
  } else {
    return lowerGenerators(lowerGenerator(std::forward<C>(chain), std::forward<G>(generator)),
                           std::forward<Gs>(more)...);
  }
}

template <typename Head, typename C>
class SetOf;
/// @section Set-builder notation
///
/// `of`, `in` and `where` write a set in set-builder notation: a head, then generators and conditions. A chain,
/// described in the Relational algebra section, writes the same sets as a sequence of steps.
///
/// The template arguments of `of` are the head: the columns of the set, in their order. Each `in` is a generator, which
/// names the columns that it binds and the source that it binds them from. `where` adds conditions over the columns of
/// the head.
///
/// `of` lowers the generators onto a chain, in the order in which they are written, and `build()` makes the rows with
/// the same engine as a chain's `build()`. As for a chain, `build()` chooses the visit order, so the generators need
/// not run in the order in which they are written.
///
/// A mistake in a generator or in the head does not compile. If this notation's own checks find the mistake, the
/// message uses the terms of this notation. If only the chain's checks find the mistake, the message uses the terms of
/// the chain. An `EntityVector` passed as an rvalue is such a mistake.
///
/// Guide: guide/set-builder-notation.md

/// @exposed
/// A set written in set-builder notation, before `build()` makes its rows: the head, and the chain that holds the
/// generators and the conditions. The type is internal. Hold a `SetOf` in an `auto` variable.
///
/// Called on an rvalue, `where` and `build()` move the chain out of the `SetOf`. Called on an lvalue, they copy the
/// chain, and the `SetOf` remains usable. `where` or `build()` called on an lvalue `SetOf` that
/// holds a temporary container does not compile, because the container lasts only until the end of the statement that
/// creates it. No type can stop `std::move` of such a `SetOf`: `where` or `build()` then compiles, and the behavior is
/// undefined. A `SetOf` over a temporary container is built in the statement that creates the container.
///
/// With Clang, `in` and `of` over a temporary container are marked with Clang's `lifetimebound` attribute. Clang then
/// warns (`-Wdangling`, on by default) at the declaration of a variable that keeps a generator or a `SetOf` over a
/// temporary container. GCC and MSVC give no such warning.
template <typename... Head, typename C>
class SetOf<ColumnList<Head...>, C> {
  using Out                        = std::tuple<Head...>;
  static constexpr bool kTemporary = ChainShape<C>::kTemporary;

 public:
  /// @hidden - made by of
  explicit SetOf(C chain) : chain_{std::move(chain)} {}

  /// Returns a new `SetOf` that keeps only the rows for which every condition in `preds` is true.
  ///
  /// The conditions of several calls to `where` combine with AND. `build()` runs each condition as soon as every
  /// column that it reads is bound.
  ///
  /// Takes: `preds` must be one or more conditions. Every column that a condition reads must be a column of the head.
  template <typename... Preds>
  [[nodiscard]] auto where(Preds... preds) && {
    checkWhere<Preds...>();
    return wrap(std::move(chain_).where(std::move(preds)...));
  }

  /// @overload
  template <typename... Preds>
  [[nodiscard]] auto where(Preds... preds) const& {
    static_assert(!kTemporary, DREAMOR_INTERNAL_SET_BUILDER_OUTLIVES_MESSAGE);
    checkWhere<Preds...>();
    return wrap(chain_.where(std::move(preds)...));
  }

  /// Makes the rows of the set.
  ///
  /// The rows come in the order of nested loops over the sources, in the visit order: the source visited first varies
  /// slowest. `visitOrder()` returns the visit order. The visit order need not be the order in which the generators
  /// are written.
  ///
  /// Returns: An `IndexSet` whose columns are in the order of the head.
  /// Contract: source_holds_no_repeat, source_holds_no_empty_ref
  [[nodiscard]] auto build() && { return ChainAccess::build<Out>(std::move(chain_)); }

  /// @overload
  [[nodiscard]] auto build() const& {
    static_assert(!kTemporary, DREAMOR_INTERNAL_SET_BUILDER_OUTLIVES_MESSAGE);
    return ChainAccess::build<Out>(chain_);
  }

  /// Returns the visit order of `build()`, as the names of the sources' columns.
  ///
  /// Each generator is one source, except a generator over a column that an earlier generator binds. Such a generator
  /// is a condition, and it is shown as follows:
  ///
  /// - If the generator's source is a range of values, the generator is shown in braces, as a set of one column.
  /// - If the generator's source is a column callable, the generator is not shown.
  ///
  /// Returns: A string in which the sources are separated by commas, and the columns of a set or of a range of rows
  /// are in braces, for example `cMachine, {cItem, cPeriod}`.
  [[nodiscard]] static constexpr std::string_view visitOrder() noexcept { return ChainAccess::visitOrder<Out, C>(); }

 private:
  template <typename... Preds>
  static consteval void checkWhere() {
    static_assert(sizeof...(Preds) > 0,
                  "where() takes the conditions it applies: .where(fits_batch), or several at once - "
                  ".where(fits_batch, is_open).");
    static_assert(
        ((!ColumnsOfPredicate<Preds>::known ||
          HasColumns<ColumnList<Head...>>::template HasAll<typename ColumnsOfPredicate<Preds>::type>::value) &&
         ...),
        "A condition takes the head's columns - of<cMachine, cItem, cPeriod>(...).where([](cMachine m, "
        "cItem i) { ... }) - and this one takes a column the head does not name.");
  }

  template <typename Next>
  [[nodiscard]] static auto wrap(Next next) {
    return SetOf<ColumnList<Head...>, Next>{std::move(next)};
  }

  C chain_;
};

// What of() does, for both of its forms: lowers the generators onto a chain and checks the head against what they
// bind.
template <typename... Head, typename... Generators>
[[nodiscard]] auto setOf(Generators&&... generators) {
  static_assert(sizeof...(Head) > 0 && (column<Head> && ...),
                "of<C...>(generators) names the set's columns first, in their order - its head: "
                "of<cMachine, cItem, cPeriod>(in<cMachine>(machines), in<cItem>(items_of), in<cPeriod>(periods)).");
  auto chain = lowerGenerators(EmptyChain{{}, {}}, std::forward<Generators>(generators)...);
  using C    = decltype(chain);
  if constexpr (std::is_same_v<C, StoppedChain>) {
    return chain;
  } else {
    static_assert(SameColumns<ColumnList<Head...>, typename ChainShape<C>::Bound>::value,
                  "The head names every column the generators bind, each once and no other: of<cMachine, cItem, "
                  "cPeriod>(in<cMachine>(machines), in<cItem>(items_of), in<cPeriod>(periods)). A column bound and "
                  "left out of the head would be a projection: build the set, then select<...>() the columns kept.");
    return SetOf<ColumnList<Head...>, C>{std::move(chain)};
  }
}

}  // namespace dreamor::internal::inline index_set

// of and in are in index_set::set_builder, named for the notation. A model makes the two short names visible with a
// using-directive in the function that writes its sets, and argument-dependent lookup does not find them.
namespace dreamor::index_set::set_builder {

/// Returns a generator that binds the columns `Cs...` from `source`.
///
/// What the generator does depends on the kind of `source`, and on the columns that the generators before it bind:
///
/// - A set or a range of rows: if `source` shares a column with the columns already bound, the generator joins the
///   rows so far with the rows of `source` on the shared columns. Otherwise, the generator pairs every row so far with
///   every row of `source`.
/// - A range of values, if the column in `Cs...` is new: the generator adds the column, with one row for each row so
///   far and each value.
/// - A range of values, if the column in `Cs...` is already bound: the generator is a condition that keeps the rows
///   whose value in the column is in the range.
/// - A column callable, if the column in `Cs...` is new: the generator adds the column. For each row so far, the
///   generator makes one row for each value that the callable returns for that row.
/// - A column callable, if the column in `Cs...` is already bound: the generator is a condition that keeps the rows
///   whose value in the column is among the values that the callable returns for the row.
///
/// A range of values is a container, an `EntityVector`, an interval, or a set of one column. A range of values fills
/// its column as in `index_set::relational::from`. A column callable returns what the callable of `Chain::lateral`
/// returns, whether it adds the column or is a condition. As a condition, an interval's value outside the range of
/// the column's integer type matches no row.
///
/// The generator holds a reference to `source`. If `source` is a temporary and the generator is used after the end of
/// the statement that creates `source`, the behavior is undefined.
///
/// Takes: `Cs...` must name at least one column, and no column twice. If `source` is a set or a range of rows,
/// `Cs...` must name every column of `source`, in any order, and no other column. If `source` is a range of values or
/// a column callable, `Cs...` must name one column. If `source` is a set of one column and `Cs...` names another
/// column, the two columns must hold the same value type. A column callable must read at least one column, and an
/// earlier generator must bind every column that the callable reads. `source` may be a temporary.
template <typename... Cs, typename Source>
[[nodiscard]] auto in(Source&& source DREAMOR_LIFETIMEBOUND) {
  static_assert(sizeof...(Cs) > 0 && (internal::column<Cs> && ...),
                "in<C...>(source) names the columns it binds: in<cMachine>(machines), not in<Ref<Machine>>, and "
                "not in(machines).");
  static_assert(internal::allColumnsDistinct<std::tuple<Cs...>>(std::index_sequence_for<Cs...>{}),
                "in<C...>(set) names each column once.");
  return internal::Generator<internal::ColumnList<Cs...>, Source&&>{
      std::forward_as_tuple(std::forward<Source>(source)),
  };
}

/// @overload
/// Returns a generator that binds the column `C` from values written in braces, in the order written.
///
/// What the generator does depends on the generators before it:
///
/// - If `C` is new, the generator adds the column, with one row for each row so far and each value.
/// - If an earlier generator binds `C`, the generator is a condition that keeps the rows whose value in `C` is one of
///   the values.
///
/// The values are a temporary container: they last until the end of the statement that writes them. If the generator
/// is used after that statement, the behavior is undefined. A `where` or `build()` called on an lvalue `SetOf` that
/// holds the values does not compile, so the set must be built in that statement.
///
/// Takes: `C` must be a column. Each value must be of the value type of `C`, or of a type that converts to it. Values
/// of any integer type may fill a column of integers, and floating-point values do not fill a column of integers. For
/// an entity column, the values must be `Ref`s, or columns or rows of one column that hold `Ref`s. For a column of
/// `std::string_view`, the values must not be `std::string`s.
/// Contract: column_integer_in_range
template <typename C, typename Value>
[[nodiscard]] auto in(std::initializer_list<Value> values DREAMOR_LIFETIMEBOUND) {
  using Braced = internal::BracedValues<Value>;
  return internal::Generator<internal::ColumnList<C>, Braced>{std::tuple<Braced>{Braced{values.begin(), values.end()}}};
}

/// Returns a `SetOf` for the rows that `generators` make, with the columns in the order of `Head...`.
///
/// `of` lowers `generators` onto a chain, in the order in which they are written. The `where` of the returned `SetOf`
/// adds conditions, and its `build()` makes the rows. To leave a bound column out of the set, build the set, and then
/// keep the other columns with `IndexSet::select`.
///
/// Takes: `Head...` must name every column that `generators` bind, each once, and no other column.
template <typename... Head, typename... Generators>
  requires(!(internal::kTemporaryGenerator<std::remove_cvref_t<Generators>> || ...))
[[nodiscard]] auto of(Generators&&... generators) {
  return internal::setOf<Head...>(std::forward<Generators>(generators)...);
}

/// @hidden - the same function for a generator over a temporary container, marked for Clang's lifetime warning
template <typename... Head, typename... Generators>
  requires(internal::kTemporaryGenerator<std::remove_cvref_t<Generators>> || ...)
[[nodiscard]] auto of(Generators&&... generators DREAMOR_LIFETIMEBOUND) {
  return internal::setOf<Head...>(std::forward<Generators>(generators)...);
}

}  // namespace dreamor::index_set::set_builder
