// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// sumOf over sets, groups and plain containers; where and split.
#include <array>
#include <cstddef>
#include <vector>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"
using cI = dreamor::Col<std::size_t, "i">;
using cJ = dreamor::Col<std::size_t, "j">;

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

int main() try {
  const IloEnv env;
  const std::vector<std::size_t> I{1, 2, 3};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::makeSet<cI, cJ>(I, J).collect();
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ, 0, 1, ILOINT, "x"};
  const auto var      = [&x](cI i, cJ j) { return x(i, j); };
  const auto weighted = [&x](cI i, cJ j) { return static_cast<double>(i.get()) * x(i, j); };

  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IJ, var).build()) == 6);
  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IJ, var).where([](cI i) { return i.get() > 1; }).build()) == 4);

  const auto byJ = IJ.groupBy<cJ>();
  for (const auto& [j, rows] : byJ) {
    DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, rows, var).build()) == 3);
  }

  const auto sums = dreamor::concert::sumOf(env, IJ, weighted)
                        .split([](cJ j) { return j.get() == 10; }, [](cJ) { return true; })
                        .build();
  DREAMOR_CHECK(terms(sums[0]) == 3 && terms(sums[1]) == 6);
  double coef = 0;
  for (auto [v, c] : sums[0].expr_->terms) {
    coef += c;
  }
  DREAMOR_CHECK(exactlyEqual(coef, 1 + 2 + 3));

  // a plain container as a one-column set
  const dreamor::concert::VarTable<cI> y{env, dreamor::makeSet<cI>(I).collect(), 0, 1};
  DREAMOR_CHECK(terms(dreamor::concert::sumOf<cI>(env, I, [&y](cI i) { return y(i); }).build()) == 3);
  // a table is itself a term: its variables, summed over the rows
  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IJ, x).build()) == 6);
  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IJ, x).where([](cI i) { return i.get() > 1; }).build()) == 4);
  for (const auto& [j, rows] : byJ) {
    DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, rows, x).build()) == 3);
  }
  const auto tableSums =
      dreamor::concert::sumOf(env, IJ, x).split([](cJ j) { return j.get() == 10; }, [](cJ) { return true; }).build();
  DREAMOR_CHECK(terms(tableSums[0]) == 3 && terms(tableSums[1]) == 6);
  using cK       = dreamor::Col<std::size_t, "k">;
  const auto IKJ = dreamor::makeSet<cI, cK, cJ>(I, I, J).collect();  // more columns than the table's keys
  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IKJ, x).build()) == 18);

  // quadratic terms: IloExpr holds them as it holds linear ones, under where() and split() too
  const auto square     = [&x](cI i, cJ j) { return static_cast<double>(i.get()) * (x(i, j) * x(i, j)); };
  const IloExpr squares = dreamor::concert::sumOf(env, IJ, square).build();
  DREAMOR_CHECK(quadraticTerms(squares) == 6 && isExactlyZero(linearCoefficients(squares)));
  DREAMOR_CHECK(exactlyEqual(quadraticCoefficients(squares), 2 * (1 + 2 + 3)));
  const auto deviation = [&x](cI i, cJ j) { return (x(i, j) - 1) * (x(i, j) - 1); };
  const auto parts     = dreamor::concert::sumOf(env, IJ, deviation)
                             .where([](cI i) { return i.get() > 1; })
                             .split([](cJ j) { return j.get() == 10; }, [](cJ) { return true; })
                             .build();
  DREAMOR_CHECK(quadraticTerms(parts[0]) == 2 && quadraticTerms(parts[1]) == 4);
  DREAMOR_CHECK(exactlyEqual(linearCoefficients(parts[1]), -2 * 4) && exactlyEqual(parts[1].expr_->constant, 4));

  // A lookup by columns inside a term reaches the row sumOf is on without hashing it. That is
  // only a hint: the same variables come back whatever the term looks up and wherever the
  // rows come from, and the hint is gone once the sum is built.
  const auto direct = varsOf(dreamor::concert::sumOf(env, IJ, x).build());
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, IJ, var).build()) == direct);
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, IJ, weighted).build()) == direct);
  const auto sameValues = IJ.where([](cI /*i*/) { return true; });
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, sameValues, var).build()) == direct);
  const auto other = [&x](cI i, cJ j) { return x(i, cJ{j.get() == 10 ? 20U : 10U}); };
  std::vector<const IloNumVarI*> swapped;
  swapped.reserve(IJ.size());
  for (const auto& [i, j] : IJ) {
    swapped.push_back(x(i, cJ{j.get() == 10 ? 20U : 10U}).getImpl());
  }
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, IJ, other).build()) == swapped);
  const auto nested = [&](cI i, cJ j) {
    DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, byJ.slice(cJ{20}), var).build()) == 3);
    return x(i, j);
  };
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, IJ, nested).build()) == direct);
  DREAMOR_CHECK(dreamor::internal::currentRow() == nullptr);

  // A table summed over a group of its own set takes its variables by the rows' positions; over a
  // group of an equal set collected apart, rows of the same type at other addresses, it looks each
  // row up. Either way the variables are those of a lookup row by row, under where() and split().
  const auto apart     = dreamor::makeSet<cI, cJ>(I, J).collect();
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
      const auto& rows = group.tuples;
      DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, rows, x).build()) == byColumns(rows, every));
      DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, rows, x).where(notTwo).build()) == byColumns(rows, notTwo));
      const auto parted = dreamor::concert::sumOf(env, rows, x).where(notTwo).split(isOne, every).build();
      DREAMOR_CHECK(varsOf(parted[0]) == byColumns(rows, [&](cI i) { return notTwo(i) && isOne(i); }));
      DREAMOR_CHECK(varsOf(parted[1]) == byColumns(rows, notTwo));
    }
  }
  return report("sum");
} catch (...) { return ::uncaught(); }
