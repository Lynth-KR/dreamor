// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// sumExpr over sets, groups and plain containers; where() and whereEach(); quadratic terms; a table as a term;
// the row hint.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {
DREAMOR_VALUE_COLUMN(cI, std::size_t);
DREAMOR_VALUE_COLUMN(cJ, std::size_t);
DREAMOR_VALUE_COLUMN(cU32, std::uint32_t);
DREAMOR_VALUE_COLUMN(cI32, std::int32_t);
DREAMOR_VALUE_COLUMN(cI64, std::int64_t);
DREAMOR_VALUE_COLUMN(cU8, std::uint8_t);
}  // namespace

static std::size_t terms(const IloExpr& e) {
  return e.expr_->terms.size();
}

static std::size_t quadraticTerms(const IloExpr& e) {
  return e.expr_->quadratic.size();
}

static double linearCoefficients(const IloExpr& e) {
  double sum = 0;
  for (const auto& [v, c] : e.expr_->terms) {
    sum += c;
  }
  return sum;
}

static double quadraticCoefficients(const IloExpr& e) {
  double sum = 0;
  for (const auto& [u, v, c] : e.expr_->quadratic) {
    sum += c;
  }
  return sum;
}

static std::vector<const IloNumVarI*> varsOf(const IloExpr& e) {
  std::vector<const IloNumVarI*> out;
  out.reserve(e.expr_->terms.size());
  for (const auto& [v, c] : e.expr_->terms) {
    out.push_back(v);
  }
  return out;
}

// A column of any integer type is a coefficient as it is, either side of a variable or an expression: as an IloNum
// (Concert's own IloNum, IloInt and int overloads leave a std::uint32_t or a std::size_t ambiguous).
template <typename C>
static double coefficientSum(const IloEnv& env) {
  const std::vector<int> values{1, 2, 3};
  const auto sC = dreamor::index_set::relational::from<C>(values).build();
  const dreamor::concert::VarTable<C> x{env, sC, 0, 1};
  const auto weighted       = [&x](C c) { return c * x(c); };
  const auto weighted_after = [&x](C c) { return x(c) * c; };
  const IloExpr left        = dreamor::concert::sumExpr(env, sC, weighted).build();
  const IloExpr right       = dreamor::concert::sumExpr(env, sC, weighted_after).build();
  const IloExpr scaled{C{2} * (left + right)};
  return linearCoefficients(scaled);
}

int main() try {
  const IloEnv env;
  const std::vector<std::size_t> I{1, 2, 3};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).build();
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ, 0, 1, ILOINT, "x"};
  const auto var      = [&x](cI i, cJ j) { return x(i, j); };
  const auto weighted = [&x](cI i, cJ j) { return static_cast<double>(i.get()) * x(i, j); };

  // a term that is an integer - a count - or a column of one adds as a number, converted by the sum, so the
  // header gives no conversion warning in a strict build
  const auto count_of  = [](cI i) { return i.get(); };
  const auto column_of = [](cJ j) { return j; };
  DREAMOR_CHECK(exactlyEqual(dreamor::concert::sumExpr(env, IJ, count_of).build().expr_->constant, 12));
  DREAMOR_CHECK(exactlyEqual(dreamor::concert::sumExpr(env, IJ, column_of).build().expr_->constant, 90));
  const auto counts = dreamor::concert::sumExpr(env, IJ, count_of).whereEach([](cJ j) { return j == 10; }).build();
  DREAMOR_CHECK(exactlyEqual(counts[0].expr_->constant, 6));

  DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, IJ, var).build()) == 6);
  DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, IJ, var).where([](cI i) { return i.get() > 1; }).build()) == 4);
  // A noexcept term, predicate and bound read as any other: noexcept is part of operator()'s type.
  // NOLINTNEXTLINE(bugprone-exception-escape): every row summed is in x's set, so no lookup throws
  const auto var_noexcept   = [&x](cI i, cJ j) noexcept { return x(i, j); };
  const auto above_noexcept = [](cI i) noexcept { return i.get() > 1; };
  const auto cap_noexcept   = [](cI i, cJ) noexcept { return static_cast<double>(i.get()); };
  DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, IJ, var_noexcept).where(above_noexcept).build()) == 4);
  const dreamor::concert::VarTable<cI, cJ> capped{env, IJ, 0, cap_noexcept, ILOFLOAT, "capped"};
  // A term that is a number - data on the set, a ParamTable - sums into the expression's constant, as Concert's
  // IloExpr += IloNum does.
  const dreamor::ParamTable<double, cI, cJ> weight{IJ, [](cI i, cJ) { return static_cast<double>(i.get()); }};
  const IloExpr numbers = dreamor::concert::sumExpr(env, IJ, weight).build();
  DREAMOR_CHECK(terms(numbers) == 0 && exactlyEqual(numbers.expr_->constant, (1 + 2 + 3) * 2));
  DREAMOR_CHECK(exactlyEqual(capped(cI{3}, cJ{10}).getUB(), 3));

  const auto byJ = IJ.groupBy<cJ>();
  for (const auto& [j, rows] : byJ) {
    DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, rows, var).build()) == 3);
  }

  const auto sums = dreamor::concert::sumExpr(env, IJ, weighted)
                        .whereEach([](cJ j) { return j.get() == 10; }, [](cJ) { return true; })
                        .build();
  DREAMOR_CHECK(terms(sums[0]) == 3 && terms(sums[1]) == 6);
  double coef = 0;
  for (auto [v, c] : sums[0].expr_->terms) {
    coef += c;
  }
  DREAMOR_CHECK(exactlyEqual(coef, 1 + 2 + 3));

  // a plain container as a one-column set
  const dreamor::concert::VarTable<cI> y{env, dreamor::index_set::relational::from<cI>(I).build(), 0, 1};
  DREAMOR_CHECK(terms(dreamor::concert::sumExpr<cI>(env, I, [&y](cI i) { return y(i); }).build()) == 3);
  // a table is itself a term: its variables, summed over the rows
  DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, IJ, x).build()) == 6);
  DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, IJ, x).where([](cI i) { return i.get() > 1; }).build()) == 4);
  for (const auto& [j, rows] : byJ) {
    DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, rows, x).build()) == 3);
  }
  const auto tableSums = dreamor::concert::sumExpr(env, IJ, x)
                             .whereEach([](cJ j) { return j.get() == 10; }, [](cJ) { return true; })
                             .build();
  DREAMOR_CHECK(terms(tableSums[0]) == 3 && terms(tableSums[1]) == 6);
  DREAMOR_VALUE_COLUMN(cK, std::size_t);
  const auto IKJ = dreamor::index_set::relational::from<cI>(I)
                       .cross<cK>(I)
                       .cross<cJ>(J)
                       .build();  // more columns than the table's keys
  DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, IKJ, x).build()) == 18);

  // quadratic terms: IloExpr holds them as it holds linear ones, under where() and whereEach() too
  const auto square     = [&x](cI i, cJ j) { return static_cast<double>(i.get()) * (x(i, j) * x(i, j)); };
  const IloExpr squares = dreamor::concert::sumExpr(env, IJ, square).build();
  DREAMOR_CHECK(quadraticTerms(squares) == 6 && isExactlyZero(linearCoefficients(squares)));
  DREAMOR_CHECK(exactlyEqual(quadraticCoefficients(squares), 2 * (1 + 2 + 3)));
  const auto deviation = [&x](cI i, cJ j) { return (x(i, j) - 1) * (x(i, j) - 1); };
  const auto parts     = dreamor::concert::sumExpr(env, IJ, deviation)
                             .where([](cI i) { return i.get() > 1; })
                             .whereEach([](cJ j) { return j.get() == 10; }, [](cJ) { return true; })
                             .build();
  DREAMOR_CHECK(quadraticTerms(parts[0]) == 2 && quadraticTerms(parts[1]) == 4);
  DREAMOR_CHECK(exactlyEqual(linearCoefficients(parts[1]), -2 * 4) && exactlyEqual(parts[1].expr_->constant, 4));

  // A lookup by columns inside a term reaches the row sumExpr is on without hashing it. That is
  // only a hint: the same variables come back whatever the term looks up and wherever the
  // rows come from, and the hint is gone once the sum is built.
  const auto direct = varsOf(dreamor::concert::sumExpr(env, IJ, x).build());
  DREAMOR_CHECK(varsOf(dreamor::concert::sumExpr(env, IJ, var).build()) == direct);
  DREAMOR_CHECK(varsOf(dreamor::concert::sumExpr(env, IJ, weighted).build()) == direct);
  const auto sameValues = IJ.where([](cI /*i*/) { return true; });
  DREAMOR_CHECK(varsOf(dreamor::concert::sumExpr(env, sameValues, var).build()) == direct);
  const auto other = [&x](cI i, cJ j) { return x(i, cJ{j.get() == 10 ? 20U : 10U}); };
  std::vector<const IloNumVarI*> swapped;
  swapped.reserve(IJ.size());
  for (const auto& [i, j] : IJ) {
    swapped.push_back(x(i, cJ{j.get() == 10 ? 20U : 10U}).getImpl());
  }
  DREAMOR_CHECK(varsOf(dreamor::concert::sumExpr(env, IJ, other).build()) == swapped);
  const auto nested = [&](cI i, cJ j) {
    DREAMOR_CHECK(terms(dreamor::concert::sumExpr(env, byJ.slice(cJ{20}), var).build()) == 3);
    return x(i, j);
  };
  DREAMOR_CHECK(varsOf(dreamor::concert::sumExpr(env, IJ, nested).build()) == direct);
  DREAMOR_CHECK(dreamor::internal::currentRow() == nullptr);

  // A table summed over a group of its own set takes its variables by the rows' positions; over a
  // group of an equal set collected apart, rows of the same type at other addresses, it looks each
  // row up. Either way the variables are those of a lookup row by row, under where() and whereEach().
  const auto apart     = dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).build();
  const auto apartByJ  = apart.groupBy<cJ>();
  const auto byColumns = [&x](const auto& rows, const auto& keep) {
    std::vector<const IloNumVarI*> out;
    for (const auto& [i, j] : rows) {
      if (keep(i)) { out.push_back(x(i, j).getImpl()); }
    }
    return out;
  };
  const auto every  = [](cI /*i*/) { return true; };
  const auto notTwo = [](cI i) { return i.get() != 2; };
  const auto isOne  = [](cI i) { return i.get() == 1; };
  for (const auto* groups : std::array{&byJ, &apartByJ}) {
    for (const auto& group : *groups) {
      const auto& rows = group.rows;
      DREAMOR_CHECK(varsOf(dreamor::concert::sumExpr(env, rows, x).build()) == byColumns(rows, every));
      DREAMOR_CHECK(varsOf(dreamor::concert::sumExpr(env, rows, x).where(notTwo).build()) == byColumns(rows, notTwo));
      const auto parted = dreamor::concert::sumExpr(env, rows, x).where(notTwo).whereEach(isOne, every).build();
      DREAMOR_CHECK(varsOf(parted[0]) == byColumns(rows, [&](cI i) { return notTwo(i) && isOne(i); }));
      DREAMOR_CHECK(varsOf(parted[1]) == byColumns(rows, notTwo));
    }
  }
  // 2 * ((1 + 2 + 3) + (1 + 2 + 3)) for every integer type
  DREAMOR_CHECK(exactlyEqual(coefficientSum<cI>(env), 24.0) && exactlyEqual(coefficientSum<cU32>(env), 24.0));
  DREAMOR_CHECK(exactlyEqual(coefficientSum<cI32>(env), 24.0) && exactlyEqual(coefficientSum<cI64>(env), 24.0));
  DREAMOR_CHECK(exactlyEqual(coefficientSum<cU8>(env), 24.0));

  return report("sum");
} catch (...) { return ::uncaught(); }
