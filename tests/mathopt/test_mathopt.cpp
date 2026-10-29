// SPDX-License-Identifier: Apache-2.0
// The MathOpt backend (run_tests.sh --mathopt): VarTable and sumOf as test_vartable and
// test_sum check them for Concert, then the lot-sizing example solved with HiGHS.
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/mathopt.hpp"
#include "float_compare.hpp"
#include "ortools/math_opt/cpp/math_opt.h"
#include "production_planning_mathopt.hpp"

namespace math_opt = ::operations_research::math_opt;

using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;

static std::size_t terms(const math_opt::LinearExpression& e) {
  return e.terms().size();
}

static void varTable() {
  math_opt::Model model("vartable");
  const std::vector<std::size_t> I{3, 1, 2};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::makeSet<ICol, JCol>(I, J).collect();

  static_assert(!std::is_copy_constructible_v<dreamor::mathopt::VarTable<ICol, JCol>>);
  static_assert(std::is_nothrow_move_constructible_v<dreamor::mathopt::VarTable<ICol, JCol>>);
  static_assert(!std::is_default_constructible_v<dreamor::mathopt::VarTable<ICol, JCol>>);

  // numbers, integer, names
  const dreamor::mathopt::VarTable<ICol, JCol> x{model, IJ, 0, 1, true, "x"};
  DREAMOR_CHECK(x.size() == 6 && model.num_variables() == 6 && x.keys().begin() == IJ.begin());
  DREAMOR_CHECK(x.contains(ICol{1}, JCol{20}) && !x.contains(ICol{9}, JCol{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(ICol{9}, JCol{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(std::tuple{ICol{9}, JCol{9}}));
  DREAMOR_CHECK(std::string{x(ICol{1}, JCol{20}).name()} == "x_1_20");
  DREAMOR_CHECK(exactlyEqual(x(ICol{3}, JCol{10}).lower_bound(), 0) &&
                exactlyEqual(x(ICol{3}, JCol{10}).upper_bound(), 1));
  DREAMOR_CHECK(x(ICol{3}, JCol{10}).is_integer());

  // iteration: (row, var) in set order
  std::size_t n = 0;
  for (const auto& [row, var] : x) {
    DREAMOR_CHECK(row == *(IJ.begin() + static_cast<std::ptrdiff_t>(n)));
    DREAMOR_CHECK(var == x(row));
    ++n;
  }
  DREAMOR_CHECK(n == 6);

  // callable bounds, continuous
  const dreamor::mathopt::VarTable<ICol, JCol> y{model,
                                                 IJ,
                                                 [](ICol i) { return -static_cast<double>(i.get()); },
                                                 [](ICol i, JCol j) { return i.get() * j.get(); },
                                                 false,
                                                 "y"};
  DREAMOR_CHECK(exactlyEqual(y(ICol{2}, JCol{20}).lower_bound(), -2) &&
                exactlyEqual(y(ICol{2}, JCol{20}).upper_bound(), 40));
  DREAMOR_CHECK(!y(ICol{2}, JCol{20}).is_integer());

  // defaults, unnamed
  const dreamor::mathopt::VarTable<ICol, JCol> z{model, IJ};
  DREAMOR_CHECK(exactlyEqual(z(ICol{3}, JCol{20}).lower_bound(), 0) &&
                exactlyEqual(z(ICol{3}, JCol{20}).upper_bound(), std::numeric_limits<double>::infinity()));
  DREAMOR_CHECK(z(ICol{3}, JCol{20}).name().empty());

  // bounds tightened afterwards, through the model as usual
  const auto atTwo = [](ICol i) { return i.get() == 2; };
  for (const auto& row : x.keys().where(atTwo)) {
    model.set_upper_bound(x(row), 0);
  }
  DREAMOR_CHECK(exactlyEqual(x(ICol{2}, JCol{10}).upper_bound(), 0) &&
                exactlyEqual(x(ICol{3}, JCol{10}).upper_bound(), 1));

  // empty set
  const auto none = IJ.where([](ICol i) { return i.get() > 100; });
  const dreamor::mathopt::VarTable<ICol, JCol> e{model, none, 0, 1};
  DREAMOR_CHECK(e.size() == 0 && e.begin() == e.end());

  // move keeps lookups working
  dreamor::mathopt::VarTable<ICol, JCol> moved{model, IJ, 0, 1, false, "w"};
  const auto before = moved(ICol{1}, JCol{10});
  const dreamor::mathopt::VarTable<ICol, JCol> moved2{std::move(moved)};
  DREAMOR_CHECK(moved2(ICol{1}, JCol{10}) == before);
}

static void sum() {
  math_opt::Model model("sum");
  const std::vector<std::size_t> I{1, 2, 3};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::makeSet<ICol, JCol>(I, J).collect();
  const dreamor::mathopt::VarTable<ICol, JCol> x{model, IJ, 0, 1, true, "x"};
  const auto var      = [&x](ICol i, JCol j) { return x(i, j); };
  const auto weighted = [&x](ICol i, JCol j) { return static_cast<double>(i.get()) * x(i, j); };

  DREAMOR_CHECK(terms(dreamor::mathopt::sumOf(IJ, var).build()) == 6);
  DREAMOR_CHECK(terms(dreamor::mathopt::sumOf(IJ, var).where([](ICol i) { return i.get() > 1; }).build()) == 4);

  const auto byJ = IJ.groupBy<JCol>();
  for (const auto& [j, rows] : byJ) {
    DREAMOR_CHECK(terms(dreamor::mathopt::sumOf(rows, var).build()) == 3);
  }

  const auto sums = dreamor::mathopt::sumOf(IJ, weighted)
                        .split([](JCol j) { return j.get() == 10; }, [](JCol) { return true; })
                        .build();
  DREAMOR_CHECK(terms(sums[0]) == 3 && terms(sums[1]) == 6);
  double coef = 0;
  for (const auto& [v, c] : sums[0].terms()) {
    coef += c;
  }
  DREAMOR_CHECK(exactlyEqual(coef, 1 + 2 + 3));

  // a table is itself a term
  DREAMOR_CHECK(terms(dreamor::mathopt::sumOf(IJ, x).build()) == 6);
  DREAMOR_CHECK(terms(dreamor::mathopt::sumOf(IJ, x).where([](ICol i) { return i.get() > 1; }).build()) == 4);

  // an empty sum is the zero expression, attached to no model
  DREAMOR_CHECK(terms(dreamor::mathopt::sumOf(IJ, var).where([](ICol) { return false; }).build()) == 0);

  // a plain container as a one-column set
  const dreamor::mathopt::VarTable<ICol> y{model, dreamor::makeSet<ICol>(I).collect(), 0, 1};
  DREAMOR_CHECK(terms(dreamor::mathopt::sumOf<ICol>(I, [&y](ICol i) { return y(i); }).build()) == 3);

  // linear terms make a LinearExpression, quadratic ones a QuadraticExpression
  static_assert(std::is_same_v<decltype(dreamor::mathopt::sumOf(IJ, weighted).build()), math_opt::LinearExpression>);
  const auto square    = [&x](ICol i, JCol j) { return static_cast<double>(i.get()) * x(i, j) * x(i, j); };
  const auto quadratic = dreamor::mathopt::sumOf(IJ, square).build();
  static_assert(std::is_same_v<std::remove_const_t<decltype(quadratic)>, math_opt::QuadraticExpression>);
  DREAMOR_CHECK(quadratic.quadratic_terms().size() == 6 && quadratic.linear_terms().empty());
  double squareCoef = 0;
  for (const auto& [pair, c] : quadratic.quadratic_terms()) {
    squareCoef += c;
  }
  DREAMOR_CHECK(exactlyEqual(squareCoef, 2 * (1 + 2 + 3)));

  // a QuadraticExpression term, under where() and split()
  const auto deviation = [&x](ICol i, JCol j) { return (x(i, j) - 1) * (x(i, j) - 1); };
  const auto parts     = dreamor::mathopt::sumOf(IJ, deviation)
                             .where([](ICol i) { return i.get() > 1; })
                             .split([](JCol j) { return j.get() == 10; }, [](JCol) { return true; })
                             .build();
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(parts[0])>, math_opt::QuadraticExpression>);
  DREAMOR_CHECK(parts[0].quadratic_terms().size() == 2 && parts[1].quadratic_terms().size() == 4);
  DREAMOR_CHECK(parts[1].linear_terms().size() == 4 && exactlyEqual(parts[1].offset(), 4));

  // an empty quadratic sum is the zero expression too
  DREAMOR_CHECK(
      dreamor::mathopt::sumOf(IJ, square).where([](ICol) { return false; }).build().quadratic_terms().empty());
}

// A quadratic objective built by sumOf, solved: HiGHS takes no quadratic objective through
// MathOpt, so this one goes to SCIP.
static void quadraticObjective() {
  math_opt::Model model("least squares");
  const std::vector<std::size_t> I{1, 2, 3};
  const auto sI = dreamor::makeSet<ICol>(I).collect();
  const dreamor::mathopt::VarTable<ICol> vFit{model, sI, -10, 10, true, "Fit"};
  const auto off_target = [&vFit](ICol i) {
    const double target = 1.4 * static_cast<double>(i.get());
    return (vFit(i) - target) * (vFit(i) - target);
  };
  model.Minimize(dreamor::mathopt::sumOf(sI, off_target).build());

  const auto result = math_opt::Solve(model, math_opt::SolverType::kGscip);
  DREAMOR_CHECK(result.ok() && result->termination.reason == math_opt::TerminationReason::kOptimal);
  const auto fitPlan = vFit.solution(*result);
  DREAMOR_CHECK(std::abs(fitPlan(ICol{1}) - 1) < 1e-6 && std::abs(fitPlan(ICol{2}) - 3) < 1e-6 &&
                std::abs(fitPlan(ICol{3}) - 4) < 1e-6);
}

static void solution() {
  // max sum i * x_i, sum x_i <= 2, x binary over I = {1, 2, 3}: x_2 = x_3 = 1.
  math_opt::Model model("solution");
  const std::vector<std::size_t> I{1, 2, 3};
  const auto sI = dreamor::makeSet<ICol>(I).collect();
  const dreamor::mathopt::VarTable<ICol> vPick{model, sI, 0, 1, true, "Pick"};
  model.AddLinearConstraint(dreamor::mathopt::sumOf(sI, [&vPick](ICol i) { return vPick(i); }).build() <= 2);
  model.Maximize(
      dreamor::mathopt::sumOf(sI, [&vPick](ICol i) { return static_cast<double>(i.get()) * vPick(i); }).build());
  const auto result = math_opt::Solve(model, math_opt::SolverType::kHighs);
  DREAMOR_CHECK(result.ok());
  if (!result.ok()) { return; }
  const auto pickPlan = vPick.solution(*result);
  DREAMOR_CHECK(pickPlan.size() == 3 && std::abs(pickPlan(ICol{1})) < 1e-6 && std::abs(pickPlan(ICol{3}) - 1) < 1e-6);
  DREAMOR_CHECK_THROWS(std::out_of_range, pickPlan(ICol{9}));

  // no primal solution: a clear exception instead of MathOpt's CHECK failure
  model.AddLinearConstraint(vPick(ICol{1}) >= 2);
  const auto infeasible = math_opt::Solve(model, math_opt::SolverType::kHighs);
  DREAMOR_CHECK(infeasible.ok() && !infeasible->has_primal_feasible_solution());
  if (infeasible.ok()) { DREAMOR_CHECK_THROWS(std::logic_error, vPick.solution(*infeasible)); }
}

static void example() {
  using namespace planning;
  for (const auto& [M, I, T] : {std::array<std::size_t, 3>{2, 3, 4}, std::array<std::size_t, 3>{3, 6, 5}}) {
    const Instance in = Instance::make(M, I, T);
    math_opt::Model model("lot sizing");
    const MathOptModel lotSizing{model, in};
    lotSizing.build(model);
    const auto capacityRows = lotSizing.sPRODUCE.groupBy<MachineCol, PeriodCol>().size();
    DREAMOR_CHECK(static_cast<std::size_t>(model.num_linear_constraints()) ==
                  lotSizing.sSTOCK.size() + capacityRows + lotSizing.sPRODUCE.size());

    const auto result = math_opt::Solve(model, math_opt::SolverType::kHighs);
    DREAMOR_CHECK(result.ok() && result->termination.reason == math_opt::TerminationReason::kOptimal);
    if (!result.ok() || !result->has_primal_feasible_solution()) { continue; }
    const auto yieldPlan = lotSizing.vYield.solution(*result);
    const auto stockPlan = lotSizing.vStock.solution(*result);

    const auto producedByItemPeriod = lotSizing.sPRODUCE.groupBy<ItemCol, PeriodCol>();
    for (const auto& [i, t] : lotSizing.sSTOCK) {
      double netFlow = (t.get() > 0 ? stockPlan(i, PeriodCol{t.get() - 1}) : 0.0) - i->demand[t.get()];
      for (const auto& row : producedByItemPeriod.slice(i, t)) {
        netFlow += yieldPlan(row);
      }
      DREAMOR_CHECK(std::abs(netFlow - stockPlan(i, t)) < 1e-6);
    }
  }
}

int main() try {
  varTable();
  sum();
  quadraticObjective();
  solution();
  example();
  return report("mathopt");
} catch (...) { return ::uncaught(); }
