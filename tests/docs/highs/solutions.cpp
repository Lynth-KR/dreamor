// SPDX-License-Identifier: MIT-0
//
// docs/guide/solutions.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <cmath>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "Highs.h"
#include "docs/guide/model.hpp"
#include "docs/highs/plan.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/param_table.hpp"
#include "dreamor/solution_table.hpp"
#include "support/check.hpp"

namespace {

using namespace guide;

bool near(double a, double b) {
  return std::abs(a - b) < 1e-6;
}

// --8<-- [start:solve]
dreamor::SolutionTable<cMachine, cItem, cPeriod> solveMake(const Sets& s,
                                                           const BomTable& quantity,
                                                           const HighsOptions& options) {
  highs::Plan plan{s, quantity, options};
  plan.env.highs().run();
  return plan.vMake.solution(plan.env);  // the plan and its solver end here, the values stay
}

// --8<-- [end:solve]

// The stock at period 1's end, kept as data for a plan from period 2 on.
dreamor::ParamTable<double, cItem, cPeriod> openingStock(const Sets& s,
                                                         const BomTable& quantity,
                                                         const HighsOptions& options) {
  highs::Plan plan{s, quantity, options};
  plan.env.highs().run();
  // --8<-- [start:carry]
  using dreamor::index_set::relational::from;
  const auto after_period_1 = [](cPeriod t) { return t == 1; };
  const auto sOPENING       = from(s.stock).where(after_period_1).build();  // each item at period 1's end

  const auto kept = plan.vStock.solution(plan.env);
  const dreamor::ParamTable<double, cItem, cPeriod> opening{sOPENING, kept};  // the next plan's opening stock
  // --8<-- [end:carry]
  return opening;
}

// The walk region in a function of its own: its row would hide read()'s (C4456 under cl.exe's /W4).
std::vector<std::string> walk(const dreamor::SolutionTable<cMachine, cItem, cPeriod>& made) {
  // --8<-- [start:walk]
  std::vector<std::string> lines;  // what the machines make, in the set's order
  for (const auto& [row, units] : made) {
    const auto& [m, i, t] = row;
    if (units > 1e-6) { lines.push_back(std::format("{} makes {:g} {} in period {}", m, units, i, t)); }
  }
  // --8<-- [end:walk]
  return lines;
}

void read(const Plant& p, const Sets& s, const BomTable& quantity, const HighsOptions& quiet) {
  const auto m1    = p.machines.at("M1");
  const auto frame = p.items.at("frame");

  // --8<-- [start:read]
  const auto made = solveMake(s, quantity, quiet);

  const double frames = made(m1, frame, 2);  // 2, the frames customers buy in period 2
  const std::tuple<cMachine, cItem, cPeriod> row{m1, frame, 3};
  const double same = made(row);  // 2, read by a row of the set
  // --8<-- [end:read]
  static_assert(std::is_same_v<decltype(made), const dreamor::SolutionTable<cMachine, cItem, cPeriod>>);
  DREAMOR_CHECK(made.size() == 15 && made.keys() == s.produce);  // a value for each row of vMake's set
  // The cost pins the frames: made on M1 alone, each period's demand in that period.
  DREAMOR_CHECK(near(frames, 2) && near(same, 2) && near(made(m1, frame, 1), 1));
  // row is a row of the set, of the type a loop over the set gives.
  static_assert(std::is_same_v<std::ranges::range_value_t<decltype(s.produce)>, std::tuple<cMachine, cItem, cPeriod>>);
  DREAMOR_CHECK(s.produce.contains(row));
  DREAMOR_CHECK(made.contains(m1, frame, 1) && !made.contains(m1, p.items.at("nut"), 1));
  DREAMOR_CHECK_THROWS(std::out_of_range, made(m1, p.items.at("nut"), 1));  // M1 makes no nuts

  const std::vector<std::string> lines = walk(made);
  // The frames, which the cost pins, come first: M1 and its first item lead the set's order.
  DREAMOR_CHECK(lines.size() >= 3 && lines[0] == "M1 makes 1 frame in period 1" &&
                lines[1] == "M1 makes 2 frame in period 2" && lines[2] == "M1 makes 2 frame in period 3");
  // The rest is the plan HiGHS finds: M1's plates in period 1, M2's plates in 3, its bolts in each period and its
  // nuts in 1 and 2.
  DREAMOR_CHECK(lines.size() == 10 && lines[3] == "M1 makes 6 plate in period 1" &&
                lines[9] == "M2 makes 13 nut in period 2");
  // M1 makes no plates in periods 2 and 3, and those rows have no line: the next line is M2's.
  DREAMOR_CHECK(lines[4] == "M2 makes 4 plate in period 3");
}

void carry(const Plant& p, const Sets& s, const BomTable& quantity, const HighsOptions& quiet) {
  const dreamor::ParamTable<double, cItem, cPeriod> opening = openingStock(s, quantity, quiet);
  DREAMOR_CHECK(opening.size() == 4);  // each needed item, at period 1's end
  DREAMOR_CHECK(opening.contains(p.items.at("plate"), 1) && !opening.contains(p.items.at("plate"), 2));
  // The plan HiGHS finds keeps 4 plates past period 1 and nothing else.
  DREAMOR_CHECK(near(opening(p.items.at("plate"), 1), 4) && near(opening(p.items.at("frame"), 1), 0) &&
                near(opening(p.items.at("bolt"), 1), 0) && near(opening(p.items.at("nut"), 1), 0));
}

void value(const Plant& p, const Sets& s, const BomTable& quantity, const HighsOptions& quiet) {
  const auto m1    = p.machines.at("M1");
  const auto frame = p.items.at("frame");
  highs::Plan plan{s, quantity, quiet};
  // Before a solve there is no solution to read.
  DREAMOR_CHECK_THROWS(std::logic_error, plan.vMake.solution(plan.env));
  DREAMOR_CHECK_THROWS(std::logic_error, dreamor::highs::val(plan.env, plan.vMake(m1, frame, 1)));
  plan.env.highs().run();

  // --8<-- [start:val]
  const double first = dreamor::highs::val(plan.env, plan.vMake(m1, frame, 1));  // 1

  const auto framesOnM1 = plan.vMake(m1, frame, 1) + plan.vMake(m1, frame, 2) + plan.vMake(m1, frame, 3);
  const double all      = dreamor::highs::val(plan.env, framesOnM1);  // 5
  // --8<-- [end:val]
  static_assert(std::is_same_v<decltype(framesOnM1), const dreamor::highs::HighsLinearExpression>);
  // The cost pins them: period 1's demand, and the three periods' 1 + 2 + 2.
  DREAMOR_CHECK(near(first, 1) && near(all, 5));
}

void noSolution(const Plant& p, const Sets& s, const BomTable& quantity, const HighsOptions& quiet) {
  const auto m1    = p.machines.at("M1");
  const auto frame = p.items.at("frame");
  highs::Plan plan{s, quantity, quiet};
  dreamor::highs::addConstr(plan.env, plan.vMake(m1, frame, 1) >= 5);  // M1's 4 hours make 4 frames at most
  plan.env.highs().run();
  DREAMOR_CHECK(plan.env.highs().getModelStatus() == HighsModelStatus::kInfeasible);
  DREAMOR_CHECK_THROWS(std::logic_error, plan.vMake.solution(plan.env));
  DREAMOR_CHECK_THROWS(std::logic_error, dreamor::highs::val(plan.env, plan.vMake(m1, frame, 1)));
}

}  // namespace

int main() try {
  const Plant p           = plant();
  const BomTable quantity = bomTable(p);
  const Sets s            = byChain(p, quantity);
  HighsOptions quiet;
  quiet.output_flag = false;
  read(p, s, quantity, quiet);
  carry(p, s, quantity, quiet);
  value(p, s, quantity, quiet);
  noSolution(p, s, quantity, quiet);
  return ::report("guide/solutions");
} catch (...) { return ::uncaught(); }
