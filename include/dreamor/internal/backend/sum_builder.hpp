// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// UnbuiltSum, which every backend's sumExpr returns before build(): the walk over the rows, where() and whereEach(),
// and the checks on the rows and the term.
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/internal/backend/row_index.hpp"
#include "dreamor/internal/index_set/binding.hpp"
#include "dreamor/internal/index_set/call.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/internal/index_set/element.hpp"
#include "dreamor/internal/index_set/grouped.hpp"
#include "dreamor/internal/index_set/key_args.hpp"

// The message for an operator on a sum that is not built, shared by every operator that rejects it.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE                                                                  \
  "sumExpr(...) is not an expression yet: add .build() - sumExpr(rows, term).build() <= 1 (sumExpr(env, rows, " \
  "term) on Concert). It stays open until then so that where() and whereEach() can still be chained."

namespace dreamor::internal::inline backend {
// Holds by pointer a term that cannot be copied, such as the VarTable in sumExpr(env, rows, vYield). The builder holds
// every other term by copy. The term must therefore outlive the builder.
template <typename T>
struct TermRef {
  const T* term;

  template <typename... Args>
  [[nodiscard]] decltype(auto) operator()(const Args&... args) const {
    return (*term)(args...);
  }
};

// The type of the term that a builder holds, through a TermRef if the builder holds the term by pointer.
template <typename Term>
struct HeldTerm {
  using type = Term;
};

template <typename T>
struct HeldTerm<TermRef<T>> {
  using type = T;
};

// Whether a sum over Rows can read Term by the positions of its rows. That is possible if the rows are a group
// (RowSlice) and the term is a table on rows of the same type. Whether the group belongs to the table's own set is
// known only at run time, and UnbuiltSum::eachRow checks it.
template <typename Rows, typename Term, typename Row>
[[nodiscard]] consteval bool byPosition() {
  if constexpr (IsRowSlice<std::remove_cvref_t<Rows>>::value) {
    using Held = HeldTerm<Term>::type;
    if constexpr (TakesRowNumber<Held>::value) { return std::is_same_v<typename Held::key_type, Row>; }
  }
  return false;
}

template <typename Term>
[[nodiscard]] auto holdTerm(Term&& term) {
  using T = std::remove_cvref_t<Term>;
  if constexpr (std::is_function_v<T>) {
    return &term;  // A function is held as a function pointer, which no_indirect_call then rejects with its message.
  } else if constexpr (std::is_lvalue_reference_v<Term> && !std::copy_constructible<T>) {
    return TermRef<T>{&term};
  } else {
    return T(std::forward<Term>(term));
  }
}

// The type of the expression that a sum adds its terms into. By default, it is the type of the backend's zero. A
// backend that keeps linear and quadratic expressions apart specializes SumTotal for a quadratic term: the total is
// then MathOpt's QuadraticExpression (mathopt.hpp) or HiGHS's HighsQuadraticExpression (highs.hpp). build() therefore
// returns the narrowest expression that holds every term. Concert's IloExpr holds every kind of term, so Concert keeps
// the default.
template <typename Zero, typename Term>
struct SumTotal {
  using type = Zero;
};

// Returns a term as the expression adds it. A number, such as the integer that m->items.size() returns, or a column
// that holds a number, becomes a double, which every backend's expression holds. The conversion is explicit here,
// because an implicit conversion inside += would make -Wconversion warn in the library's header. Any other term is
// returned as it is.
template <typename T>
[[nodiscard]] decltype(auto) addedTerm(T&& term) {
  using Bare = std::remove_cvref_t<T>;
  if constexpr (std::is_arithmetic_v<Bare>) {
    return static_cast<double>(term);
  } else if constexpr (column<Bare>) {
    if constexpr (std::is_arithmetic_v<typename Bare::value_type>) {
      return static_cast<double>(term.get());
    } else {
      return std::forward<T>(term);
    }
  } else {
    return std::forward<T>(term);
  }
}

/// @section Sums
///
/// The parts of this section add up a term over rows into a solver's expression. Each backend has its own `sumExpr`,
/// in that backend's section. `sumExpr` returns a sum that is not built yet, so that `where` and `whereEach` can
/// follow it. `build()` then makes the solver's expression. An operator applied to a sum that is not built, such as
/// `<=` or `+`, does not compile, and the error message names `build()`.
///
/// The rows of a sum are one of these:
///
/// - A set. The sum holds a copy of the set, which shares the rows of the set and copies none of them.
/// - The rows of one group of a `Grouped`, which `slice` returns. The rows remain valid only while the `Grouped`
///   exists, so the `Grouped` must outlive the sum.
/// - A container whose elements are values of one column. The column is the one template argument of `sumExpr`, and
///   the container's elements fill the column as they fill the column of `from`. The sum refers to the container and
///   does not copy it, so the container must outlive the sum. A container given as an rvalue does not compile.
///
/// The term of a sum is one of these:
///
/// - A column callable that reads columns of the rows and returns the row's term: a variable, an expression or a
///   number. The sum holds a copy of the callable.
/// - A `VarTable` whose key columns are all columns of the rows. The term of each row is the variable of the row's
///   key. The sum refers to the table and does not copy it, so the table must outlive the sum.
///
/// Guide: guide/sums.md

/// @exposed
/// A sum of a term over rows that is not built yet. The sum holds the rows, the term, and the conditions of `where`
/// and `whereEach`.
///
/// A model calls the members of this class and does not name the class. The class's name appears in a compiler's
/// error message, such as a message about a conversion from `UnbuiltSum` to `IloExpr`.
template <typename Rows,
          typename Term,
          typename MakeZero,
          typename Filters = std::tuple<>,
          typename Splits  = std::tuple<>>
class UnbuiltSum {
  // MakeZero returns the backend's empty expression, and build() adds each term to it with +=. This class therefore
  // names no solver type.
 public:
  // Without these operators, a compiler's error lists every operator<= in scope. They are hidden friends, so they are
  // candidates only if an UnbuiltSum is an operand. They are templates, so their match is exact. The return type is
  // deduced, so that the static_assert is instantiated as soon as overload resolution chooses the operator. The
  // message then comes before any error from the call that receives the result, such as model.add(bool).

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator<=(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator<=(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator>=(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator>=(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator==(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator==(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator<(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator<(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator>(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator>(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return false;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator+(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator+(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator-(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator-(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator*(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator*(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator/(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator/(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator+=(Other& other, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return other;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Other>
  friend auto operator-=(Other& other, const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Other>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return other;
  }

  /// @hidden - exists only to reject, with a message, an operator on a sum that is not built
  template <typename Self = UnbuiltSum>
  friend auto operator-(const UnbuiltSum& /*sum*/) {
    static_assert(always_false<Self>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
    return 0;
  }

  /// @hidden - the row type of the rows, which a model does not name
  using Row = std::remove_cvref_t<std::ranges::range_value_t<Rows>>;

  /// @hidden - called only by sumExpr, where and whereEach
  UnbuiltSum(MakeZero zero, Rows rows, Term term, Filters filters, Splits splits)
      : zero_{std::move(zero)},
        rows_{std::move(rows)},
        term_{std::move(term)},
        filters_{std::move(filters)},
        splits_{std::move(splits)} {}

  /// Returns a sum over the rows of this sum for which every condition in `preds` holds.
  ///
  /// If `where` is called again on the returned sum, the conditions of both calls apply: a row is kept only if every
  /// condition holds for it. This sum is left unchanged.
  ///
  /// Takes: Each element of `preds` must be a column callable that reads columns of the rows and returns a value that
  /// converts to `bool`.
  template <typename... Ps>
    requires(sizeof...(Ps) > 0)
  [[nodiscard]] auto where(Ps... preds) const {
    (checkColumnCallable<Ps, Row>(), ...);
    auto next = std::tuple_cat(filters_, std::make_tuple(std::move(preds)...));
    return UnbuiltSum<Rows, Term, MakeZero, decltype(next), Splits>{zero_, rows_, term_, std::move(next), splits_};
  }

  /// Returns a sum that `build()` makes into one expression for each condition in `preds`.
  ///
  /// `build()` on the returned sum returns a `std::array` with one expression for each element of `preds`, in the
  /// order of `preds`. For the `k`-th condition `p_k`, the expression is the sum of `term(r)` over the rows `r` that
  /// the conditions of `where` keep and for which `p_k(r)` holds. If several conditions hold for a row, the row's term
  /// is added to the expression of each of them. `build()` reads the rows once, and computes the term of a row at most
  /// once. This sum is left unchanged.
  ///
  /// Takes: Each element of `preds` must be a column callable that reads columns of the rows and returns a value that
  /// converts to `bool`. Every condition must be given in one call: `whereEach` on a sum that `whereEach` returned does
  /// not compile.
  template <typename... Ps>
    requires(sizeof...(Ps) > 0)
  [[nodiscard]] auto whereEach(Ps... preds) const {
    (checkColumnCallable<Ps, Row>(), ...);
    static_assert(std::tuple_size_v<Splits> == 0,
                  "whereEach() once: pass every predicate together, `whereEach(p1, p2, p3)`. They "
                  "are alternatives sharing one traversal, not a chain that narrows.");
    auto next = std::tuple_cat(splits_, std::make_tuple(std::move(preds)...));
    return UnbuiltSum<Rows, Term, MakeZero, Filters, decltype(next)>{zero_, rows_, term_, filters_, std::move(next)};
  }

  /// Returns the solver's expression that adds up the term of each row that the conditions of `where` keep.
  ///
  /// The expression is the sum of `term(r)` over those rows `r`. If no row is kept, the expression is zero.
  ///
  /// The type of the expression depends on the backend:
  ///
  /// - On Concert, an `IloExpr`.
  /// - On MathOpt, a `LinearExpression`, or a `QuadraticExpression` if the term is quadratic.
  /// - On HiGHS, a `HighsLinearExpression`, or a `HighsQuadraticExpression` if the term is quadratic.
  ///
  /// If the sum has the conditions of a `whereEach`, `build()` returns a `std::array` of these expressions instead,
  /// one for each condition, as `whereEach` describes. A term that is an integer, or a column of numbers, is converted
  /// to `double` before it is added. Each call to `build()` makes a new expression, and the sum is left unchanged.
  ///
  /// Throws: `std::out_of_range` if the term is a `VarTable` and the key of a row is not in the table's set.
  [[nodiscard]] auto build() {
    // Not const, because a std::ranges::filter_view given as the rows has no const begin().
    if constexpr (std::tuple_size_v<Splits> == 0) {
      auto total = zero();
      eachRow([&](const auto& row, const auto& term) {
        if (keeps(row)) { total += addedTerm(term()); }
      });
      return total;
    } else {
      return buildSplit(std::make_index_sequence<std::tuple_size_v<Splits>>{});
    }
  }

 private:
  // Calls visit(row, term) on each row, where term() returns the term of the row. If the rows are a group of the set
  // that the table is on, the group's positions are the positions of the variables, so the term is read by position,
  // with no lookup of the row's address. Any other rows go through termOf: a group of another set with rows of the
  // same type, a set, or a container.
  template <typename Visit>
  void eachRow(const Visit& visit) {
    if constexpr (byPosition<Rows, Term, Row>()) {
      using Held             = HeldTerm<Term>::type;
      const Held& table      = heldTerm();
      const Row* const first = SliceAccess::first(rows_);
      if (first == TakesRowNumber<Held>::firstRow(table)) {
        for (const std::uint32_t at : SliceAccess::positions(rows_)) {
          visit(first[at], [&] { return TakesRowNumber<Held>::at(table, at); });
        }
        return;
      }
    }
    for (auto&& row : rows_) {
      visit(row, [&]() -> decltype(auto) { return termOf(row); });
    }
  }

  // Returns the term, through the TermRef if the builder holds the term by pointer.
  [[nodiscard]] const auto& heldTerm() const noexcept {
    if constexpr (std::is_same_v<typename HeldTerm<Term>::type, Term>) {
      return term_;
    } else {
      return *term_.term;
    }
  }

  // Returns the backend's empty expression, of the type that SumTotal chooses for the terms.
  [[nodiscard]] auto zero() const {
    using Value = std::remove_cvref_t<decltype(termOf(std::declval<const Row&>()))>;
    using Total = SumTotal<decltype(zero_()), Value>::type;
    return Total(zero_());
  }

  // Returns the term of one row. A term that takes the whole row, such as a table, finds the row by its address. For
  // any other term, the row is published while the term runs, so that the tables that the term looks up by columns
  // can find the row without hashing.
  template <typename R>
  [[nodiscard]] decltype(auto) termOf(const R& row) const {
    if constexpr (TakesWholeRow<Term>::value) {
      return callColumnsOf(term_, row);
    } else {
      const CurrentRowScope scope{std::addressof(row)};
      return callColumnsOf(term_, row);
    }
  }

  // Checks whether every condition of where() holds for the row.
  template <typename R>
  [[nodiscard]] bool keeps(const R& row) const {
    return std::apply([&](const auto&... preds) { return (static_cast<bool>(callColumnsOf(preds, row)) && ...); },
                      filters_);
  }

  template <std::size_t... Is>
  [[nodiscard]] auto buildSplit(std::index_sequence<Is...> /*unused*/) {
    std::array<decltype(zero()), sizeof...(Is)> totals{(static_cast<void>(Is), zero())...};
    eachRow([&](const auto& row, const auto& term) {
      if (!keeps(row)) { return; }
      const std::array<bool, sizeof...(Is)> hits{static_cast<bool>(callColumnsOf(std::get<Is>(splits_), row))...};
      if (std::ranges::none_of(hits, std::identity{})) { return; }
      const auto term_value = addedTerm(term());
      ((hits[Is] ? static_cast<void>(totals[Is] += term_value) : static_cast<void>(0)), ...);
    });
    return totals;
  }

  MakeZero zero_;
  Rows rows_;
  Term term_;
  Filters filters_;
  Splits splits_;
};

// Whether T is a sum that is not built. An objective given such a sum is rejected with the message above.
template <typename T>
inline constexpr bool kIsUnbuiltSum = false;
template <typename Rows, typename Term, typename MakeZero, typename Filters, typename Splits>
inline constexpr bool kIsUnbuiltSum<UnbuiltSum<Rows, Term, MakeZero, Filters, Splits>> = true;

// A plain container takes one column type, as index_set::relational::from<C>(range) does. A set already has its
// columns, so it takes none.
//
// A container is one column, so at most one column type is accepted. For more columns, a model builds a set with a
// chain, and a model usually reuses such a set anyway.
//
// The rows must be a set or a group of one, such as the RowSlice that Grouped::slice() returns. A temporary container
// does not compile: std::views::all would own it in an owning view, which cannot be copied, so where() could not
// copy the rows into the next sum.
template <typename T>
inline constexpr bool kIsGroup = false;
template <typename Tup, typename... Columns>
inline constexpr bool kIsGroup<::dreamor::Group<Tup, Columns...>> = true;

template <typename... Columns, typename MakeZero, typename Rows, typename Term>
[[nodiscard]] auto makeSumOver(MakeZero zero, Rows&& rows, Term&& term);

// What every backend's sumExpr calls. It accepts anything as the rows, so that rows that are not a range are
// rejected with a message, not with an error that no sumExpr matches.
template <typename... Columns, typename MakeZero, typename Rows, typename Term>
[[nodiscard]] auto makeSum(MakeZero zero, Rows&& rows, Term&& term) {
  constexpr bool kRange = std::ranges::input_range<Rows>;
  static_assert(kRange,
                "sumExpr sums over a range of rows - a set, a group's rows byMachine.slice(m), or a container "
                "naming its column, sumExpr<cMachine>(machines, cost) - and this is not one: over an Ordered, "
                "sum its set (with the env first on Concert).");
  if constexpr (kRange) {
    return makeSumOver<Columns...>(std::move(zero), std::forward<Rows>(rows), std::forward<Term>(term));
  }
}

template <typename... Columns, typename MakeZero, typename Rows, typename Term>
[[nodiscard]] auto makeSumOver(MakeZero zero, Rows&& rows, Term&& term) {
  using Element = std::remove_cvref_t<std::ranges::range_value_t<std::remove_reference_t<Rows>>>;
  static_assert(sizeof...(Columns) <= 1,
                "One column type at most: a plain container is one column. Build a set over "
                "several columns - index_set::relational::from<cMachine>(machines).cross<cItem>(items).build() - then "
                "sumExpr(that_set, term) (sumExpr(env, that_set, term) on Concert).");

  if constexpr (sizeof...(Columns) == 1) {
    static_assert((column<Columns> && ...),
                  "Name the column: sumExpr<cMachine>(machines, cost) (the env first on Concert), not "
                  "sumExpr<Ref<Machine>>(machines, cost). Declare it once with "
                  "`DREAMOR_ENTITY_COLUMN(cMachine, Machine);`.");
    static_assert(!IsTuple<Element>::value,
                  "These rows already are an index set, and it brings its own column types: "
                  "sumExpr(sASSIGN, term), not sumExpr<cMachine>(sASSIGN, term) (the env first on Concert).");
    static_assert((refsFromTheirVector<Rows, Columns>() && ...), DREAMOR_INTERNAL_REFS_FROM_THEIR_VECTOR_MESSAGE);
    return makeSum(std::move(zero), namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), std::forward<Term>(term));
  } else {
    static_assert(!kIsGroup<Element>,
                  "A Grouped is a range of groups, not of rows: sum one group's rows - sumExpr(byMachine.slice(m), "
                  "term) - or the set itself (the env first on Concert).");
    static_assert(IsTuple<Element>::value || kIsGroup<Element>,
                  "sumExpr takes an index set: a range of rows of columns, as a chain's build() "
                  "returns. To sum over a plain container, name its column: "
                  "sumExpr<cMachine>(machines, cost) (sumExpr<cMachine>(env, machines, cost) on Concert).");
    if constexpr (IsTuple<Element>::value) {
      static_assert(
          allElementsAreColumns<Element>(std::make_index_sequence<std::tuple_size_v<Element>>{}),
          "These rows are tuples of plain values, not of named columns. Build the "
          "set with index_set::relational::from<cMachine>(...).cross<cItem>(...), so the term and the predicates "
          "can name what they read.");
    }
    using Held = decltype(holdTerm(std::forward<Term>(term)));
    checkColumnCallable<Held, Element>();
    auto view = std::views::all(std::forward<Rows>(rows));
    static_assert(
        std::copy_constructible<decltype(view)>,
        "sumExpr keeps its rows by reference, so they cannot be a temporary container: name it first - const auto "
        "periodList = periods(); - then sumExpr<cPeriod>(periodList, term) (sumExpr<cPeriod>(env, periodList, term) "
        "on Concert).");
    return UnbuiltSum<decltype(view), Held, MakeZero>{
        std::move(zero),
        std::move(view),
        holdTerm(std::forward<Term>(term)),
        {},
        {},
    };
  }
}

}  // namespace dreamor::internal::inline backend

// A TermRef lists the same columns as the term it points to, and takes the whole row if that term does.
template <typename T>
struct dreamor::internal::CallableColumns<dreamor::internal::TermRef<T>> : dreamor::internal::CallableColumns<T> {};

template <typename T>
struct dreamor::internal::TakesWholeRow<dreamor::internal::TermRef<T>> : dreamor::internal::TakesWholeRow<T> {};
