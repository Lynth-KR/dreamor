// SPDX-License-Identifier: MIT-0
//
// docs/guide/constraints-and-objective.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

#include "Highs.h"
#include "docs/guide/model.hpp"
#include "docs/highs/plan.hpp"
#include "dreamor/highs.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {

using namespace guide;
using namespace dreamor::highs;

// A row of HiGHS's model by its name; -1 for none.
HighsInt rowOf(const HighsLp& lp, std::string_view name) {
  const auto found = std::ranges::find(lp.row_names_, name);
  return found == lp.row_names_.end() ? -1 : static_cast<HighsInt>(found - lp.row_names_.begin());
}

// How many columns a row of HiGHS's model holds.
HighsInt rowEntries(const HighsLp& lp, HighsInt row) {
  const HighsSparseMatrix& a = lp.a_matrix_;
  if (a.isRowwise()) { return a.start_[row + 1] - a.start_[row]; }
  return static_cast<HighsInt>(std::ranges::count(a.index_, row));
}

// A row's coefficient on a column in HiGHS's model; 0 where the row does not hold the column.
double coefficientIn(const HighsLp& lp, HighsInt row, HighsInt column) {
  const HighsSparseMatrix& a = lp.a_matrix_;
  const HighsInt major       = a.isRowwise() ? row : column;
  const HighsInt minor       = a.isRowwise() ? column : row;
  for (HighsInt k = a.start_[major]; k < a.start_[major + 1]; ++k) {
    if (a.index_[k] == minor) { return a.value_[k]; }
  }
  return 0;
}

// How many rows of HiGHS's model are named <family>_...
std::ptrdiff_t rowsOf(const HighsLp& lp, const std::string& family) {
  const auto in_family = [&family](const std::string& name) { return name.starts_with(family + "_"); };
  return std::ranges::count_if(lp.row_names_, in_family);
}

HighsOptions quietOptions() {
  HighsOptions quiet;
  quiet.output_flag = false;
  return quiet;
}

void rowPerRow(const Plant& p) {
  const BomTable q = bomTable(p);
  const guide::highs::Plan plan{byChain(p, q), q, quietOptions()};
  const HighsLp& lp = plan.env.highs().getLp();

  // The hours region: one row a group of produce's rows by machine and period, 2 machines times 3 periods.
  DREAMOR_CHECK(rowsOf(lp, "Hours") == 6);
  // The setups region: one row a row of produce - M1's frame and plate, M2's plate, bolt and nut, in 3 periods.
  DREAMOR_CHECK(rowsOf(lp, "Setup") == 15);
  DREAMOR_CHECK(lp.num_row_ == 12 + 6 + 15);  // balance, hours, setups
  // Hours_M1_1 holds M1's frame and plate in period 1 (the bracket is not needed), up to M1's 4 hours.
  const HighsInt hoursM1 = rowOf(lp, "Hours_M1_1");
  DREAMOR_CHECK(hoursM1 >= 0 && rowEntries(lp, hoursM1) == 2);
  DREAMOR_CHECK(exactlyEqual(lp.row_upper_[hoursM1], 4.0) && exactlyEqual(lp.row_lower_[hoursM1], -kHighsInf));
  const HighsInt hoursM2 = rowOf(lp, "Hours_M2_3");
  DREAMOR_CHECK(hoursM2 >= 0 && rowEntries(lp, hoursM2) == 3 && exactlyEqual(lp.row_upper_[hoursM2], 3.0));
  // Setup_M2_nut_3: the nuts M2 makes in period 3 less their most times the setup, at most 0. M2 works 3 hours and a
  // nut takes 0.1, so the most is 30.
  const HighsInt setupNut = rowOf(lp, "Setup_M2_nut_3");
  DREAMOR_CHECK(setupNut >= 0 && rowEntries(lp, setupNut) == 2 && exactlyEqual(lp.row_upper_[setupNut], 0.0));
  const auto m2  = p.machines.at("M2");
  const auto nut = p.items.at("nut");
  DREAMOR_CHECK(exactlyEqual(coefficientIn(lp, setupNut, plan.vMake(m2, nut, 3).index()), 1.0));
  DREAMOR_CHECK(std::abs(coefficientIn(lp, setupNut, plan.vSetup(m2, nut, 3).index()) + 30.0) < 1e-9);
}

void balance(const Plant& p) {
  const BomTable q = bomTable(p);
  const guide::highs::Plan plan{byChain(p, q), q, quietOptions()};
  const HighsLp& lp = plan.env.highs().getLp();

  DREAMOR_CHECK(rowsOf(lp, "Balance") == 12);  // 4 needed items (frame, plate, bolt, nut) in 3 periods
  // == fixes both bounds to the demand: the frame's 1 in period 1, the nut's 5 in period 2.
  const HighsInt frame1 = rowOf(lp, "Balance_frame_1");
  DREAMOR_CHECK(frame1 >= 0 && exactlyEqual(lp.row_lower_[frame1], 1.0) && exactlyEqual(lp.row_upper_[frame1], 1.0));
  const HighsInt nut2 = rowOf(lp, "Balance_nut_2");
  DREAMOR_CHECK(nut2 >= 0 && exactlyEqual(lp.row_lower_[nut2], 5.0) && exactlyEqual(lp.row_upper_[nut2], 5.0));
  // Plates in period 1: made on M1 and on M2, taken by M1's frames, kept - 4 columns, no stock carried in. Period 2
  // adds the plates kept from period 1.
  DREAMOR_CHECK(rowEntries(lp, rowOf(lp, "Balance_plate_1")) == 4);
  DREAMOR_CHECK(rowEntries(lp, rowOf(lp, "Balance_plate_2")) == 5);
}

void twoBoundsAndHandle(const Plant& p) {
  const BomTable q = bomTable(p);
  guide::highs::Plan plan{byChain(p, q), q, quietOptions()};
  const auto m1    = p.machines.at("M1");
  const auto frame = p.items.at("frame");

  // --8<-- [start:two-bounds]
  // M1 makes no fewer frames in period 2 than in period 1, and at most 5 more
  const auto ramp = (0 <= plan.vMake(m1, frame, 2) - plan.vMake(m1, frame, 1)) <= 5;
  // --8<-- [end:two-bounds]
  static_assert(std::is_same_v<decltype(ramp), const HighsBoundedLinearExpression>);
  // 0 <= ... alone is a row with only its lower bound; <= 5 adds the upper one.
  static_assert(std::is_same_v<decltype(0 <= plan.vMake(m1, frame, 2) - plan.vMake(m1, frame, 1)),
                               HighsLowerBoundedLinearExpression>);
  DREAMOR_CHECK(exactlyEqual(ramp.lower(), 0.0) && exactlyEqual(ramp.upper(), 5.0));
  DREAMOR_CHECK(ramp.expression().size() == 2);

  // --8<-- [start:handle]
  const HighsCons rampRow = addConstr(plan.env, ramp, dreamor::indexedName("Ramp", m1, frame, 2));
  plan.env.highs().changeRowBounds(rampRow.index(), 0, 1);  // at most 1 more
  // --8<-- [end:handle]
  const HighsLp& lp = plan.env.highs().getLp();
  DREAMOR_CHECK(rampRow.index() == 33);  // after the plan's 33 rows, counted from 0
  DREAMOR_CHECK(&rampRow.env() == &plan.env);
  DREAMOR_CHECK(rowOf(lp, "Ramp_M1_frame_2") == rampRow.index());
  DREAMOR_CHECK(exactlyEqual(lp.row_lower_[rampRow.index()], 0.0) && exactlyEqual(lp.row_upper_[rampRow.index()], 1.0));
}

template <typename Expression>
concept growable = requires(Expression& e, HighsVar x) { e += x; };

void grow(const Plant& p) {
  const BomTable q = bomTable(p);
  const guide::highs::Plan plan{byChain(p, q), q, quietOptions()};
  const BomTable& quantity = q;
  const auto m1            = p.machines.at("M1");
  const auto frame         = p.items.at("frame");
  const auto plate         = p.items.at("plate");
  const auto bolt          = p.items.at("bolt");

  // --8<-- [start:grow]
  // The bolts M1's frames and plates take in period 1; declared auto, bolts would be a single term, with no +=
  HighsLinearExpression bolts = quantity(cParent{frame}, cChild{bolt}) * plan.vMake(m1, frame, 1);
  bolts += quantity(cParent{plate}, cChild{bolt}) * plan.vMake(m1, plate, 1);
  // --8<-- [end:grow]
  // A quantity times a variable is a single term, which has no +=.
  static_assert(
      std::is_same_v<decltype(quantity(cParent{frame}, cChild{bolt}) * plan.vMake(m1, frame, 1)), HighsLinearTerm>);
  static_assert(!growable<HighsLinearTerm> && growable<HighsLinearExpression>);
  DREAMOR_CHECK(bolts.size() == 2);
  DREAMOR_CHECK(bolts.indices()[0] == plan.vMake(m1, frame, 1).index() &&
                bolts.indices()[1] == plan.vMake(m1, plate, 1).index());
  DREAMOR_CHECK(exactlyEqual(bolts.coefficients()[0], 4.0) && exactlyEqual(bolts.coefficients()[1], 2.0));
}

void objective(const Plant& p) {
  const BomTable q = bomTable(p);
  guide::highs::Plan plan{byChain(p, q), q, quietOptions()};
  const HighsLp& lp = plan.env.highs().getLp();
  const auto m1     = p.machines.at("M1");
  const auto frame  = p.items.at("frame");

  // The cost region: a column's cost is its coefficient - M1's setup 10, a frame held a period 2.0.
  DREAMOR_CHECK(lp.sense_ == ObjSense::kMinimize);
  DREAMOR_CHECK(exactlyEqual(lp.col_cost_[plan.vSetup(m1, frame, 1).index()], 10.0));
  DREAMOR_CHECK(exactlyEqual(lp.col_cost_[plan.vStock(frame, 1).index()], 2.0));
  DREAMOR_CHECK(isExactlyZero(lp.col_cost_[plan.vMake(m1, frame, 1).index()]));
  plan.env.highs().run();
  DREAMOR_CHECK(plan.env.highs().getModelStatus() == HighsModelStatus::kOptimal);
  DREAMOR_CHECK(std::abs(plan.env.highs().getInfo().objective_function_value - 75.4) < 1e-6);
}

void leastStock(const Plant& p) {
  const BomTable q = bomTable(p);
  const Sets s     = byChain(p, q);
  guide::highs::Plan plan{s, q, quietOptions()};

  // --8<-- [start:minimize]
  const HighsStatus status = minimize(plan.env, sumExpr(s.stock, plan.vStock).build());  // the least stock held
  // --8<-- [end:minimize]
  DREAMOR_CHECK(status == HighsStatus::kOk);
  DREAMOR_CHECK(plan.env.highs().getModelStatus() == HighsModelStatus::kOptimal);
  DREAMOR_CHECK(plan.env.highs().getLp().sense_ == ObjSense::kMinimize);
  // The setups' costs are gone: only the stock columns cost.
  const HighsInt setupM1Frame = plan.vSetup(p.machines.at("M1"), p.items.at("frame"), 1).index();
  DREAMOR_CHECK(isExactlyZero(plan.env.highs().getLp().col_cost_[setupM1Frame]));
  // Each period's needs fit its hours - period 2: M1 makes 2 frames and 4 plates (4 hours), M2 16 bolts and 13 nuts
  // (2.9 hours) - so nothing need be kept.
  DREAMOR_CHECK(std::abs(plan.env.highs().getInfo().objective_function_value) < 1e-9);
}

}  // namespace

int main() try {
  const Plant p = plant();
  rowPerRow(p);
  balance(p);
  twoBoundsAndHandle(p);
  grow(p);
  objective(p);
  leastStock(p);
  return ::report("guide/constraints-and-objective");
} catch (...) { return ::uncaught(); }
