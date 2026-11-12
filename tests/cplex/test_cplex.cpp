// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Against the real CPLEX (the cplex preset): a small MIP built with the library, solved,
// read back with solution(), and exported to check that key-based names reach CPLEX; then a
// quadratic objective summed by sumExpr, solved as an MIQP; columns of std::uint32_t and std::size_t as
// coefficients, and a family with a variable no row uses.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {
DREAMOR_VALUE_COLUMN(cI, std::size_t);
DREAMOR_VALUE_COLUMN(cJ, std::size_t);
DREAMOR_VALUE_COLUMN(cPeriod, std::uint32_t);
}  // namespace

int main() try {
  IloEnv env;
  try {
    const std::vector<std::size_t> I{1, 2, 3, 4};
    const std::vector<std::size_t> J{10, 20, 30};
    const auto IJ = dreamor::index_set::relational::from<cI>(I)
                        .cross<cJ>(J)
                        .where([](cI i, cJ j) { return (i.get() + j.get()) % 3 != 0; })
                        .build();
    const auto II = dreamor::index_set::relational::from<cI>(I).build();

    const dreamor::concert::VarTable<cI, cJ> x{env, IJ, 0, 1, ILOBOOL, "x"};
    const dreamor::concert::VarTable<cI> slack{
        env, II, 0, [](cI i) { return static_cast<double>(i.get()); }, ILOFLOAT, "s"};
    const dreamor::concert::VarTable<cI, cJ> sc{dreamor::concert::semiContinuous, env, IJ, 2, 5, ILOFLOAT, "sc"};
    DREAMOR_CHECK(exactlyEqual(slack(cI{3}).getUB(), 3));

    const IloModel model(env);
    const auto xv  = [&x](cI i, cJ j) { return x(i, j); };
    const auto byI = IJ.groupBy<cI>();
    for (const auto& [i, rows] : byI) {
      model.add(dreamor::concert::sumExpr(env, rows, xv).build() <= 1);
    }
    const auto byJ = IJ.groupBy<cJ>();
    for (const auto& [j, rows] : byJ) {
      model.add(dreamor::concert::sumExpr(env, rows, xv).build() <= 2);
    }
    for (const auto& [row, v] : sc) {
      model.add(v <= 5.0 * x(row));
    }
    for (const auto& [row, v] : slack) {
      model.add(v >= 0.5 * dreamor::concert::sumExpr(env, byI.slice(row), xv).build());
    }

    IloExpr obj = dreamor::concert::sumExpr(env, IJ, [&x](cI i, cJ j) {
                    return static_cast<double>(((i.get() * 7) + j.get()) % 11) * x(i, j);
                  }).build();
    obj += dreamor::concert::sumExpr(env, II, [&slack](cI i) { return -1.0 * slack(i); }).build();
    obj += dreamor::concert::sumExpr(env, IJ, [&sc](cI i, cJ j) { return 0.01 * sc(i, j); }).build();
    model.add(IloMaximize(env, obj));
    obj.end();

    IloCplex cplex(model);
    cplex.setOut(env.getNullStream());
    DREAMOR_CHECK(cplex.solve());
    DREAMOR_CHECK(cplex.getStatus() == IloAlgorithm::Optimal);

    // solution(): one call, same values as asking variable by variable
    const auto vx = x.solution(cplex);
    DREAMOR_CHECK(vx.size() == x.size());
    for (const auto& [row, var] : x) {
      DREAMOR_CHECK(std::abs(vx(row) - cplex.getValue(var)) < 1e-9);
    }
    for (const auto& [i, rows] : byI) {
      double sum = 0;
      for (const auto& row : rows) {
        sum += vx(row);
      }
      DREAMOR_CHECK(sum <= 1 + 1e-6);
    }
    const auto vsc = sc.solution(cplex);
    for (const auto& [row, v] : vsc) {
      DREAMOR_CHECK(v < 1e-6 || (v > 2 - 1e-6 && v < 5 + 1e-6));
    }

    // key-based names reach CPLEX
    const std::string lp = "dreamor_test_cplex.lp";
    cplex.exportModel(lp.c_str());
    std::stringstream text;
    text << std::ifstream(lp).rdbuf();
    DREAMOR_CHECK(text.str().find("x_1_10") != std::string::npos && text.str().find("s_3") != std::string::npos);
    std::remove(lp.c_str());
    cplex.end();

    // a quadratic objective built by sumExpr: integer least squares, an MIQP
    const IloModel squares(env);
    const dreamor::concert::VarTable<cI> fit{env, II, -10, 10, ILOINT, "Fit"};
    const auto off_target = [&fit](cI i) {
      const double target = 1.4 * static_cast<double>(i.get());
      return (fit(i) - target) * (fit(i) - target);
    };
    IloExpr sumOfSquares = dreamor::concert::sumExpr(env, II, off_target).build();
    squares.add(IloMinimize(env, sumOfSquares));
    sumOfSquares.end();
    IloCplex leastSquares(squares);
    leastSquares.setOut(env.getNullStream());
    DREAMOR_CHECK(leastSquares.solve());
    DREAMOR_CHECK(leastSquares.getStatus() == IloAlgorithm::Optimal);
    const auto fitPlan = fit.solution(leastSquares);
    DREAMOR_CHECK(std::abs(fitPlan(cI{1}) - 1) < 1e-6 && std::abs(fitPlan(cI{2}) - 3) < 1e-6 &&
                  std::abs(fitPlan(cI{3}) - 4) < 1e-6 && std::abs(fitPlan(cI{4}) - 6) < 1e-6);
    leastSquares.end();

    // a column of std::uint32_t or std::size_t as a coefficient, either side - Concert's own overloads would leave
    // both ambiguous - and a variable no row uses, which CPLEX does not extract, read as 0
    const IloModel weighted(env);
    const auto sPERIODS = dreamor::index_set::relational::from<cPeriod>(dreamor::interval::closed(1, 3)).build();
    const dreamor::concert::VarTable<cPeriod> run{env, sPERIODS, 0, 1, ILOFLOAT, "Run"};
    const auto late   = [](cPeriod t) { return t > 1; };
    const auto weight = [&run](cPeriod t) { return t * run(t) + run(t) * t; };
    IloExpr lateness  = dreamor::concert::sumExpr(env, sPERIODS, weight).where(late).build();  // Run_1 unused
    weighted.add(IloMaximize(env, lateness));
    weighted.add(dreamor::concert::sumExpr(env, II, [&run](cI i) { return i * run(cPeriod{3}); }).build() <= 30);
    lateness.end();
    IloCplex weighing(weighted);
    weighing.setOut(env.getNullStream());
    DREAMOR_CHECK(weighing.solve());
    const auto runPlan = run.solution(weighing);
    DREAMOR_CHECK(isExactlyZero(runPlan(cPeriod{1})) && std::abs(runPlan(cPeriod{3}) - 1) < 1e-6);
    DREAMOR_CHECK(std::abs(weighing.getObjValue() - 10) < 1e-6);  // 2 * 2 + 2 * 3
    weighing.end();
  } catch (const IloException& e) {
    std::fprintf(stderr, "IloException: %s\n", e.getMessage());
    ++g_failures;
  }
  env.end();
  return report("cplex");
} catch (...) { return ::uncaught(); }
