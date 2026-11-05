// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Against the real CPLEX (run_tests.sh --cplex): a small MIP built with the library, solved,
// read back with solution(), and exported to check that key-based names reach CPLEX; then a
// quadratic objective summed by sumExpr, solved as an MIQP.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"
using cI = dreamor::Col<std::size_t, "i">;
using cJ = dreamor::Col<std::size_t, "j">;

int main() try {
  IloEnv env;
  try {
    const std::vector<std::size_t> I{1, 2, 3, 4};
    const std::vector<std::size_t> J{10, 20, 30};
    const auto IJ =
        dreamor::makeSet<cI, cJ>(I, J).where([](cI i, cJ j) { return (i.get() + j.get()) % 3 != 0; }).build();
    const auto II = dreamor::makeSet<cI>(I).build();

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
  } catch (const IloException& e) {
    std::fprintf(stderr, "IloException: %s\n", e.getMessage());
    ++g_failures;
  }
  env.end();
  return report("cplex");
} catch (...) { return ::uncaught(); }
