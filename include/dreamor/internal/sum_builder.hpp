// SPDX-License-Identifier: Apache-2.0
// File: internal/sum_builder.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// The backend-free part of sumOf: the traversal, where() / split(), and the compile-time
// checks on rows and callables. A backend's sumOf only says how to make an empty
// expression - see concert.hpp, which also carries the user-facing description.
#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/index_set/binding.hpp"
#include "dreamor/index_set/call.hpp"
#include "dreamor/index_set/column.hpp"
#include "dreamor/index_set/element.hpp"
#include "dreamor/index_set/key_args.hpp"
#include "dreamor/internal/row_index.hpp"

namespace dreamor::internal {

// How the builder holds its term: a copy, except for a term that cannot be copied - a
// variable table, sumOf(env, rows, vYield) - which is held by reference for the one
// expression the builder lives in.
template <typename T>
struct TermRef {
  const T* term;
  template <typename... Args>
  [[nodiscard]] decltype(auto) operator()(const Args&... args) const {
    return (*term)(args...);
  }
};

template <typename Term>
[[nodiscard]] auto holdTerm(Term&& term) {
  using T = std::remove_cvref_t<Term>;
  if constexpr (std::is_lvalue_reference_v<Term> && !std::copy_constructible<T>) {
    return TermRef<T>{&term};
  } else {
    return T(std::forward<Term>(term));
  }
}

}  // namespace dreamor::internal

// A held reference names the same columns as what it refers to, and takes what it takes.
template <typename T>
struct dreamor::PredicateColumns<dreamor::internal::TermRef<T>> : dreamor::PredicateColumns<T> {};
template <typename T>
struct dreamor::internal::TakesWholeRow<dreamor::internal::TermRef<T>> : dreamor::internal::TakesWholeRow<T> {};

namespace dreamor::internal {

// A plain container becomes a one-column index set in place: no vector of tuples is built,
// the column is wrapped as each element is visited.
template <typename Column, typename Rows>
[[nodiscard]] auto namedAsOneColumn(Rows&& rows) {
  return std::views::all(std::forward<Rows>(rows)) |
         std::views::transform([](auto&& element) { return std::tuple{toColumn<Column>(element)}; });
}

// sumOf()'s result before build(): rows, a term, and the where() / split() so far. It is not
// an expression yet, on purpose - so the chain can go on - and its name says what is missing
// wherever a compiler prints it: "conversion from UnbuiltSum<...> to IloExpr".
//
// MakeZero returns the backend's empty expression (IloExpr{env}, a LinearExpression, ...);
// build() adds term(row) to it with +=, so this never names a solver type itself.
template <typename Rows,
          typename Term,
          typename MakeZero,
          typename Filters = std::tuple<>,
          typename Splits  = std::tuple<>>
class UnbuiltSum {
 public:
  // The operators a user reaches for when build() is forgotten - sumOf(...) <= 1,
  // total += sumOf(...) - each stop on the fix. Without them a compiler lists every
  // operator<= in scope, standard library included: 288 lines under GCC 13. Hidden friends,
  // so they are candidates only when an UnbuiltSum is an operand; templates, so they match
  // exactly and win over anything that would need a conversion. The return type is deduced
  // so that the body - the static_assert - is instantiated the moment one is chosen, before
  // whatever the result was passed to (model.add(bool)) can report first.
  template <typename Other>
  friend auto operator<=(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator<=(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator>=(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator>=(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator==(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator==(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator<(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator<(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator>(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator>(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return false;
  }
  template <typename Other>
  friend auto operator+(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }
  template <typename Other>
  friend auto operator+(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }
  template <typename Other>
  friend auto operator-(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }
  template <typename Other>
  friend auto operator-(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }
  template <typename Other>
  friend auto operator*(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }
  template <typename Other>
  friend auto operator*(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }
  template <typename Other>
  friend auto operator/(const UnbuiltSum& /*sum*/, const Other& /*other*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }
  template <typename Other>
  friend auto operator/(const Other& /*other*/, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }
  template <typename Other>
  friend auto operator+=(Other& other, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return other;
  }
  template <typename Other>
  friend auto operator-=(Other& other, const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Other>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return other;
  }
  template <typename Self = UnbuiltSum>
  friend auto operator-(const UnbuiltSum& /*sum*/) {
    static_assert(
        always_false<Self>,
        "sumOf(...) is not an expression yet: add .build() - sumOf(env, rows, term).build() <= 1. It stays open until then so that where() and split() can still be chained.");
    return 0;
  }

  using Row = std::remove_cvref_t<std::ranges::range_value_t<Rows>>;

  // Built by sumOf() and rebuilt by where()/split(); not meant to be spelled out at call
  // sites.
  UnbuiltSum(MakeZero zero, Rows rows, Term term, Filters filters, Splits splits)
      : zero_{std::move(zero)},
        rows_{std::move(rows)},
        term_{std::move(term)},
        filters_{std::move(filters)},
        splits_{std::move(splits)} {}

  // Narrows the rows, the same way makeSet's where() does: chaining is AND, and neither
  // the parameter order nor the chaining order matters.
  template <typename... Ps>
    requires(sizeof...(Ps) > 0)
  [[nodiscard]] auto where(Ps... preds) const {
    (checkColumnCallable<Ps, Row>(), ...);
    auto next = std::tuple_cat(filters_, std::make_tuple(std::move(preds)...));
    return UnbuiltSum<Rows, Term, MakeZero, decltype(next), Splits>{zero_, rows_, term_, std::move(next), splits_};
  }

  // One sum per predicate, over the rows that where() left, in a single traversal. The
  // result is an array in this order; term() still runs at most once per row.
  template <typename... Ps>
    requires(sizeof...(Ps) > 0)
  [[nodiscard]] auto split(Ps... preds) const {
    (checkColumnCallable<Ps, Row>(), ...);
    static_assert(std::tuple_size_v<Splits> == 0,
                  "split() once: pass every predicate together, `split(p1, p2, p3)`. They "
                  "are alternatives sharing one traversal, not a chain that narrows.");
    auto next = std::tuple_cat(splits_, std::make_tuple(std::move(preds)...));
    return UnbuiltSum<Rows, Term, MakeZero, Filters, decltype(next)>{zero_, rows_, term_, filters_, std::move(next)};
  }

  // Non-const: filtered views have no const begin().
  [[nodiscard]] auto build() {
    if constexpr (std::tuple_size_v<Splits> == 0) {
      auto total = zero_();
      for (auto&& row : rows_) {
        if (!keeps(row)) {
          continue;
        }
        total += termOf(row);
      }
      return total;
    } else {
      return buildSplit(std::make_index_sequence<std::tuple_size_v<Splits>>{});
    }
  }

 private:
  // The term on one row. A term that takes the row whole - a table - already knows it; any
  // other gets the row published while it runs, for the tables it looks up by columns.
  template <typename R>
  [[nodiscard]] decltype(auto) termOf(const R& row) const {
    if constexpr (TakesWholeRow<Term>::value) {
      return callColumnsOf(term_, row);
    } else {
      const CurrentRowScope scope{std::addressof(row)};
      return callColumnsOf(term_, row);
    }
  }

  // Every where() predicate, ANDed.
  template <typename R>
  [[nodiscard]] bool keeps(const R& row) const {
    return std::apply([&](const auto&... preds) { return (static_cast<bool>(callColumnsOf(preds, row)) && ...); },
                      filters_);
  }

  template <std::size_t... Is>
  [[nodiscard]] auto buildSplit(std::index_sequence<Is...> /*unused*/) {
    std::array<decltype(zero_()), sizeof...(Is)> totals{(static_cast<void>(Is), zero_())...};
    for (auto&& row : rows_) {
      if (!keeps(row)) {
        continue;
      }
      const std::array<bool, sizeof...(Is)> hits{static_cast<bool>(callColumnsOf(std::get<Is>(splits_), row))...};
      if (std::ranges::none_of(hits, std::identity{})) {
        continue;
      }
      const auto term_value = termOf(row);
      ((hits[Is] ? static_cast<void>(totals[Is] += term_value) : static_cast<void>(0)), ...);
    }
    return totals;
  }

  MakeZero zero_;
  Rows rows_;
  Term term_;
  Filters filters_;
  Splits splits_;
};

// An index set, or a group of one - Grouped hands back a RowSlice, which is fine.
// A temporary container is not: views::all owns it, and an owning view cannot be copied, so
// where() would have nothing to chain from.
// One explicit column names a plain container's column in place, the way makeSet does:
//     sumOf<MachineCol>(env, machines, cost)      =  sum {m in MACHINES} cost(m)
// No column types means the rows are already an index set.
// sum {m in MACHINES} names both its index and its set, so a plain container takes one column
// type, exactly as in makeSet. An index set already carries its own, so it takes none:
//
//     sum {m in MACHINES} cost(m)         sumOf<MachineCol>(env, machines, cost)
//     sum {(m,i) in ASSIGN} cost(m,i)     sumOf(env, sASSIGN, cost)
//
// One column at most - a container is one column. For more, build the set with makeSet; a
// set over several columns is something the model reuses anyway.
//
// Rows must be an index set or a group of one; Grouped::slice() hands back a RowSlice,
// which is fine. A temporary container is not: views::all owns it, and an owning view cannot be
// copied, so where() would have nothing to chain from.
template <typename... Columns, typename MakeZero, std::ranges::input_range Rows, typename Term>
[[nodiscard]] auto makeSum(MakeZero zero, Rows&& rows, Term&& term) {
  using Element = std::remove_cvref_t<std::ranges::range_value_t<std::remove_reference_t<Rows>>>;
  static_assert(sizeof...(Columns) <= 1,
                "One column type at most: a plain container is one column. Build a set over "
                "several columns with makeSet, then sumOf(env, that_set, term).");

  if constexpr (sizeof...(Columns) == 1) {
    static_assert((is_column<Columns> && ...),
                  "Name the column: sumOf<MachineCol>(env, machines, cost), not "
                  "sumOf<const Machine*>(env, machines, cost). Declare it once with "
                  "`using MachineCol = Col<const Machine*, \"machine\">;`.");
    static_assert(!IsTuple<Element>::value,
                  "These rows already are an index set, and it brings its own column types: "
                  "sumOf(env, ASSIGN, term), not sumOf<OrderCol>(env, ASSIGN, term).");
    return makeSum(std::move(zero), namedAsOneColumn<Columns...>(std::forward<Rows>(rows)), std::forward<Term>(term));
  } else {
    static_assert(IsTuple<Element>::value,
                  "sumOf takes an index set: a range of column tuples, as makeSet(...)."
                  "collect() returns. To sum over a plain container, name its column: "
                  "sumOf<MachineCol>(env, machines, cost).");
    if constexpr (IsTuple<Element>::value) {
      static_assert(allElementsAreColumns<Element>(std::make_index_sequence<std::tuple_size_v<Element>>{}),
                    "These rows are tuples of plain values, not of named columns. Build the "
                    "set with makeSet<MachineCol, ItemCol>(...), so the term and the predicates "
                    "can name what they read.");
    }
    using Held = decltype(holdTerm(std::forward<Term>(term)));
    checkColumnCallable<Held, Element>();
    auto view = std::views::all(std::forward<Rows>(rows));
    static_assert(std::copy_constructible<decltype(view)>,
                  "Bind the set to a variable first - `const auto sASSIGN = makeSet<MachineCol, "
                  "ItemCol>(machines, items).collect();` - then sumOf(env, sASSIGN, term). A "
                  "temporary container is held by an owning view, which cannot be copied.");
    return UnbuiltSum<decltype(view), Held, MakeZero>{
        std::move(zero),
        std::move(view),
        holdTerm(std::forward<Term>(term)),
        {},
        {},
    };
  }
}

}  // namespace dreamor::internal
