// SPDX-License-Identifier: MIT-0
//
// docs/guide/sums.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "Highs.h"
#include "docs/guide/model.hpp"
#include "docs/highs/plan.hpp"
#include "dreamor/highs.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {

using namespace guide;
using namespace dreamor::highs;

// An expression's coefficient on each variable, by the variable's column, repeats added.
std::map<HighsInt, double> termsOf(const HighsLinearExpression& expression) {
  std::map<HighsInt, double> terms;
  for (std::size_t k = 0; k < expression.size(); ++k) {
    terms[expression.indices()[k]] += expression.coefficients()[k];
  }
  return terms;
}

// A row of the solver's model, found by its name: its coefficient on each variable.
std::map<HighsInt, double> rowNamed(Highs& highs, const std::string& name) {
  HighsInt row = -1;
  DREAMOR_CHECK(highs.getRowByName(name, row) == HighsStatus::kOk);
  HighsInt rows    = 0;
  HighsInt nonzero = 0;
  double lower     = 0;
  double upper     = 0;
  HighsInt start   = 0;
  std::vector<HighsInt> columns(static_cast<std::size_t>(highs.getNumCol()));
  std::vector<double> values(columns.size());
  highs.getRows(1, &row, rows, &lower, &upper, nonzero, &start, columns.data(), values.data());
  std::map<HighsInt, double> terms;
  for (HighsInt k = 0; k < nonzero; ++k) {
    terms[columns[static_cast<std::size_t>(k)]] += values[static_cast<std::size_t>(k)];
  }
  return terms;
}

// Whether a row of the solver's model, found by its name, has these bounds.
bool rowBoundsAre(Highs& highs, const std::string& name, double lower, double upper) {
  HighsInt row = -1;
  DREAMOR_CHECK(highs.getRowByName(name, row) == HighsStatus::kOk);
  const HighsLp& lp = highs.getLp();
  const auto at     = static_cast<std::size_t>(row);
  return exactlyEqual(lp.row_lower_[at], lower) && exactlyEqual(lp.row_upper_[at], upper);
}

bool near(double a, double b) {
  return std::abs(a - b) < 1e-6;
}

void sums(const Plant& p) {
  const BomTable q = bomTable(p);
  const Sets s     = byChain(p, q);
  HighsOptions quiet;
  quiet.output_flag = false;
  guide::highs::Plan plan{s, q, quiet};
  const auto& vMake  = plan.vMake;
  const auto& vStock = plan.vStock;
  const auto m1      = p.machines.at("M1");
  const auto m2      = p.machines.at("M2");
  const auto frame   = p.items.at("frame");
  const auto plate   = p.items.at("plate");
  const auto bolt    = p.items.at("bolt");
  const auto nut     = p.items.at("nut");

  // --8<-- [start:over-a-set]
  const auto hours = [&vMake](cMachine m, cItem i, cPeriod t) { return i->time * vMake(m, i, t); };

  const HighsLinearExpression totalHours = sumExpr(s.produce, hours).build();  // the hours of every run
  // --8<-- [end:over-a-set]
  static_assert(!std::is_convertible_v<decltype(sumExpr(s.produce, hours)), HighsLinearExpression>);
  // A term for each of produce's 15 rows, each the item's hours on its variable.
  DREAMOR_CHECK(totalHours.size() == 15 && termsOf(totalHours).size() == 15);
  DREAMOR_CHECK(exactlyEqual(termsOf(totalHours).at(vMake(m1, frame, 1).index()), 1.0));
  DREAMOR_CHECK(exactlyEqual(termsOf(totalHours).at(vMake(m2, nut, 3).index()), 0.1));

  // --8<-- [start:slice]
  const auto byMachinePeriod = s.produce.groupBy<cMachine, cPeriod>();

  const HighsLinearExpression m2Hours = sumExpr(byMachinePeriod.slice(m2, 1), hours).build();  // M2 in period 1
  // --8<-- [end:slice]
  // M2 makes the plate (0.5 hours), the bolt and the nut (0.1 hours each).
  const std::map<HighsInt, double> m2Terms{
      {vMake(m2, plate, 1).index(), 0.5},
      {vMake(m2, bolt, 1).index(), 0.1},
      {vMake(m2, nut, 1).index(), 0.1},
  };
  DREAMOR_CHECK(m2Hours.size() == 3 && termsOf(m2Hours) == m2Terms);
  // The plan's hours region: one row for each machine in each period, 6, and M2's in period 1 is m2Hours.
  Highs& highs  = plan.env.highs();
  int hoursRows = 0;
  for (HighsInt row = 0; row < highs.getNumRow(); ++row) {
    std::string name;
    highs.getRowName(row, name);
    if (name.starts_with("Hours_")) { ++hoursRows; }
  }
  DREAMOR_CHECK(hoursRows == 6);
  DREAMOR_CHECK(rowNamed(highs, "Hours_M2_1") == m2Terms);
  DREAMOR_CHECK(rowBoundsAre(highs, "Hours_M2_1", -kHighsInf, 3.0));  // up to M2's 3 hours

  // --8<-- [start:table-term]
  const auto byMachine = s.produce.groupBy<cMachine>();

  const HighsLinearExpression m2Stock = sumExpr(byMachine.slice(m2), vStock).build();  // what M2 makes, kept
  // --8<-- [end:table-term]
  // M2's rows are its plate, bolt and nut in 3 periods: 9 rows, each picking vStock(item, period) once.
  std::map<HighsInt, double> m2StockTerms;
  for (const auto i : {plate, bolt, nut}) {
    for (int t = 1; t <= 3; ++t) {
      m2StockTerms[vStock(i, t).index()] = 1.0;
    }
  }
  DREAMOR_CHECK(m2Stock.size() == 9 && termsOf(m2Stock) == m2StockTerms);
  // The plan's balance region, the plate in period 1: made on M1 and on M2, taken 2 a frame made on M1 (the bracket
  // that takes a plate too is not needed, so not made), kept.
  const std::map<HighsInt, double> plateBalance{
      {vMake(m1, plate, 1).index(), 1.0},
      {vMake(m2, plate, 1).index(), 1.0},
      {vMake(m1, frame, 1).index(), -2.0},
      {vStock(plate, 1).index(), -1.0},
  };
  DREAMOR_CHECK(rowNamed(highs, "Balance_plate_1") == plateBalance);
  DREAMOR_CHECK(rowBoundsAre(highs, "Balance_plate_1", 0.0, 0.0));  // customers buy no plates
  // The bracket is not needed, so s.produce makes none, and s.consume holds none of its lines.
  const auto bracket = p.items.at("bracket");
  bool bracketMade   = false;
  for (const auto& [parent, child, machine, period] : s.consume) {
    bracketMade = bracketMade || parent == bracket;
  }
  DREAMOR_CHECK(!bracketMade && !s.consume.contains(cParent{bracket}, cChild{plate}, m1, 1));

  // --8<-- [start:where]
  const auto in_period_1 = [](cPeriod t) { return t == 1; };
  const auto is_sold     = [](cItem i) { return std::ranges::max(i->demand) > 0; };
  const auto is_unsold   = [&is_sold](cItem i) { return !is_sold(i); };

  const HighsLinearExpression firstHours = sumExpr(s.produce, hours).where(in_period_1).build();
  const auto [soldHours, unsoldHours]    = sumExpr(s.produce, hours).whereEach(is_sold, is_unsold).build();
  // --8<-- [end:where]
  static_assert(std::is_same_v<decltype(sumExpr(s.produce, hours).whereEach(is_sold, is_unsold).build()),
                               std::array<HighsLinearExpression, 2>>);
  // Period 1: M1's frame and plate, M2's plate, bolt and nut. Sold: the frame (M1) and the nut (M2) in 3 periods;
  // unsold: M1's plate, M2's plate and bolt in 3 periods.
  DREAMOR_CHECK(firstHours.size() == 5 && termsOf(firstHours).contains(vMake(m2, nut, 1).index()));
  DREAMOR_CHECK(soldHours.size() == 6 && termsOf(soldHours).contains(vMake(m1, frame, 2).index()));
  DREAMOR_CHECK(unsoldHours.size() == 9 && termsOf(unsoldHours).contains(vMake(m2, bolt, 3).index()));

  // --8<-- [start:container]
  const int last         = p.periods;
  const auto kept_at_end = [&vStock, last](cItem i) { return vStock(i, last); };

  const HighsLinearExpression m2Left = sumExpr<cItem>(m2->items, kept_at_end).build();  // M2's items, left over
  // --8<-- [end:container]
  const std::map<HighsInt, double> m2LeftTerms{
      {vStock(plate, 3).index(), 1.0},
      {vStock(bolt, 3).index(), 1.0},
      {vStock(nut, 3).index(), 1.0},
  };
  DREAMOR_CHECK(m2Left.size() == 3 && termsOf(m2Left) == m2LeftTerms);

  // Solved: whatever the timing, each item is made as often as the demand and the parents made take, and nothing is
  // kept past period 3 (holding costs). 5 frames, 10 plates (2 a frame), 40 bolts (4 a frame, 2 a plate), 25 nuts
  // (2 a plate, 5 sold): 5 + 5 + 4 + 2.5 = 16.5 hours, 7.5 of them on what is sold (frames, nuts).
  highs.run();
  DREAMOR_CHECK(highs.getModelStatus() == HighsModelStatus::kOptimal);
  DREAMOR_CHECK(near(highs.getInfo().objective_function_value, 75.4));
  DREAMOR_CHECK(near(val(plan.env, totalHours), 16.5));
  DREAMOR_CHECK(near(val(plan.env, soldHours), 7.5) && near(val(plan.env, unsoldHours), 9.0));
  DREAMOR_CHECK(near(val(plan.env, m2Left), 0.0));
}

}  // namespace

int main() try {
  const Plant p = plant();
  sums(p);
  return ::report("guide/sums");
} catch (...) { return ::uncaught(); }
