// SPDX-License-Identifier: Apache-2.0
// sumOf over sets, groups and plain containers; where and split.
#include <cstddef>
#include <vector>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"
using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;

static std::size_t terms(const IloExpr& e) {
  return e.expr_->terms.size();
}

static std::vector<const IloNumVarI*> varsOf(const IloExpr& e) {
  std::vector<const IloNumVarI*> out;
  out.reserve(e.expr_->terms.size());
  for (const auto& [v, c] : e.expr_->terms) out.push_back(v);
  return out;
}

int main() try {
  const IloEnv env;
  const std::vector<std::size_t> I{1, 2, 3};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::makeSet<ICol, JCol>(I, J).collect();
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ, 0, 1, ILOINT, "x"};
  const auto var      = [&x](ICol i, JCol j) { return x(i, j); };
  const auto weighted = [&x](ICol i, JCol j) { return static_cast<double>(i.get()) * x(i, j); };

  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IJ, var).build()) == 6);
  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IJ, var).where([](ICol i) { return i.get() > 1; }).build()) == 4);

  const auto byJ = IJ.groupBy<JCol>();
  for (const auto& [j, rows] : byJ) DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, rows, var).build()) == 3);

  const auto sums = dreamor::concert::sumOf(env, IJ, weighted)
                        .split([](JCol j) { return j.get() == 10; }, [](JCol) { return true; })
                        .build();
  DREAMOR_CHECK(terms(sums[0]) == 3 && terms(sums[1]) == 6);
  double coef = 0;
  for (auto [v, c] : sums[0].expr_->terms) coef += c;
  DREAMOR_CHECK(exactlyEqual(coef, 1 + 2 + 3));

  // a plain container as a one-column set
  const dreamor::concert::VarTable<ICol> y{env, dreamor::makeSet<ICol>(I).collect(), 0, 1};
  DREAMOR_CHECK(terms(dreamor::concert::sumOf<ICol>(env, I, [&y](ICol i) { return y(i); }).build()) == 3);
  // a table is itself a term: its variables, summed over the rows
  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IJ, x).build()) == 6);
  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IJ, x).where([](ICol i) { return i.get() > 1; }).build()) == 4);
  for (const auto& [j, rows] : byJ) DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, rows, x).build()) == 3);
  const auto tableSums = dreamor::concert::sumOf(env, IJ, x)
                             .split([](JCol j) { return j.get() == 10; }, [](JCol) { return true; })
                             .build();
  DREAMOR_CHECK(terms(tableSums[0]) == 3 && terms(tableSums[1]) == 6);
  using KCol     = dreamor::Col<std::size_t, "k">;
  const auto IKJ = dreamor::makeSet<ICol, KCol, JCol>(I, I, J).collect();  // more columns than the table's keys
  DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, IKJ, x).build()) == 18);

  // A lookup by columns inside a term reaches the row sumOf is on without hashing it. That is
  // only a hint: the same variables come back whatever the term looks up and wherever the
  // rows come from, and the hint is gone once the sum is built.
  const auto direct = varsOf(dreamor::concert::sumOf(env, IJ, x).build());
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, IJ, var).build()) == direct);
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, IJ, weighted).build()) == direct);
  const auto sameValues = IJ.where([](ICol /*i*/) { return true; });
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, sameValues, var).build()) == direct);
  const auto other = [&x](ICol i, JCol j) { return x(i, JCol{j.get() == 10 ? 20U : 10U}); };
  std::vector<const IloNumVarI*> swapped;
  swapped.reserve(IJ.size());
  for (const auto& [i, j] : IJ) swapped.push_back(x(i, JCol{j.get() == 10 ? 20U : 10U}).getImpl());
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, IJ, other).build()) == swapped);
  const auto nested = [&](ICol i, JCol j) {
    DREAMOR_CHECK(terms(dreamor::concert::sumOf(env, byJ.slice(JCol{20}), var).build()) == 3);
    return x(i, j);
  };
  DREAMOR_CHECK(varsOf(dreamor::concert::sumOf(env, IJ, nested).build()) == direct);
  DREAMOR_CHECK(dreamor::internal::currentRow() == nullptr);
  return report("sum");
} catch (...) {
  return ::uncaught();
}
