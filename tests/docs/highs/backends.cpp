// SPDX-License-Identifier: MIT-0
//
// docs/guide/backends.md: the page shows the regions between --8<-- lines, and the checks stay here.
#include <cmath>
#include <limits>
#include <string>
#include <type_traits>

#include "Highs.h"
#include "docs/guide/model.hpp"
#include "docs/highs/plan.hpp"
#include "docs/log_capture.hpp"
#include "dreamor/highs.hpp"
#include "dreamor/logger.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {

using namespace guide;
using namespace dreamor::highs;

bool near(double a, double b) {
  return std::abs(a - b) < 1e-6;
}

HighsOptions quietOptions() {
  HighsOptions quiet;
  quiet.output_flag = false;
  return quiet;
}

void giveOptions(const Plant& p, const BomTable& quantity) {
  // --8<-- [start:options]
  HighsOptions options;
  options.output_flag = false;  // no log
  options.time_limit  = 60;     // seconds
  guide::highs::Plan plan{byChain(p, quantity), quantity, options};
  // --8<-- [end:options]
  const HighsOptions& held = plan.env.highs().getOptions();
  DREAMOR_CHECK(!held.output_flag && exactlyEqual(held.time_limit, 60));
  DREAMOR_CHECK(plan.env.highs().getLp().num_col_ == 42);
  // The options reach the Highs before the model does: an environment reached with nothing written has them.
  const HighsEnv empty{options};
  DREAMOR_CHECK(exactlyEqual(empty.highs().getOptions().time_limit, 60) && empty.highs().getLp().num_col_ == 0);
  static_assert(!std::is_copy_constructible_v<HighsEnv> && !std::is_move_constructible_v<HighsEnv>);
}

void reach(const Plant& p, const BomTable& quantity) {
  const HighsOptions quiet = quietOptions();

  // --8<-- [start:reach]
  const Sets s = byChain(p, quantity);
  guide::highs::Plan plan{s, quantity, quiet};  // written into plan.env: the Highs holds nothing yet
  Highs& highs = plan.env.highs();              // the first reach passes the model whole

  const auto kept = sumExpr(s.stock, plan.vStock).build();
  addConstr(plan.env, kept <= 10, "StockCap");  // stock over every item and period, at most 10: a row at once
  // --8<-- [end:reach]
  DREAMOR_CHECK(highs.getLp().num_col_ == 42 && highs.getLp().num_row_ == 34);
  DREAMOR_CHECK(highs.getLp().row_names_.back() == "StockCap");
  highs.run();
  DREAMOR_CHECK(highs.getModelStatus() == HighsModelStatus::kOptimal);
  DREAMOR_CHECK(near(highs.getInfo().objective_function_value, 81.3));

  // The first reach of another plan finds it whole: 15 make + 15 setup + 12 stock columns, 12 balance + 6 hours +
  // 15 setup rows.
  guide::highs::Plan other{s, quantity, quiet};
  DREAMOR_CHECK(other.env.highs().getLp().num_col_ == 42 && other.env.highs().getLp().num_row_ == 33);
}

void ownApi(const Plant& p, const BomTable& quantity) {
  guide::highs::Plan plan{byChain(p, quantity), quantity, quietOptions()};
  const LogCapture log;
  DREAMOR_CHECK(log.isOpen());

  // --8<-- [start:own-api]
  Highs& highs = plan.env.highs();
  highs.run();
  const double cost = highs.getInfo().objective_function_value;  // 75.4

  const HighsVar plateOnM2 = plan.vSetup(p.machines.at("M2"), p.items.at("plate"), 3);
  highs.changeColBounds(plateOnM2.index(), 0, 0);  // no plates on M2 in period 3
  highs.run();
  if (highs.getModelStatus() != HighsModelStatus::kOptimal) {
    DREAMOR_LOG_WARNING("no plan without plates on M2 in period 3");
  }
  const double costWithout = highs.getInfo().objective_function_value;  // 79.8
  // --8<-- [end:own-api]
  DREAMOR_CHECK(near(cost, 75.4));
  DREAMOR_CHECK(highs.getModelStatus() == HighsModelStatus::kOptimal);
  DREAMOR_CHECK(near(costWithout, 79.8));
  DREAMOR_CHECK(log.text().empty());  // HiGHS found an optimal plan: no warning
  DREAMOR_CHECK(isExactlyZero(highs.getLp().col_upper_[static_cast<std::size_t>(plateOnM2.index())]));
  // Without the setup, the setup row allows M2 no plates in period 3.
  const HighsVar madeOnM2 = plan.vMake(p.machines.at("M2"), p.items.at("plate"), 3);
  DREAMOR_CHECK(near(highs.getSolution().col_value[static_cast<std::size_t>(madeOnM2.index())], 0));
}

void failingCall(const Plant& p, const BomTable& quantity) {
  const LogCapture log;
  DREAMOR_CHECK(log.isOpen());

  // --8<-- [start:error]
  HighsOptions options;
  options.time_limit = -1;  // HiGHS takes no negative time limit
  try {
    guide::highs::Plan plan{byChain(p, quantity), quantity, options};
    plan.env.highs().run();
  } catch (const HighsCallError& error) {
    DREAMOR_LOG_ERROR("{}", error.what());  // dreamor::highs: Highs::passOptions returned Error
  }
  // --8<-- [end:error]
  DREAMOR_CHECK(log.text().ends_with("[ERROR] dreamor::highs: Highs::passOptions returned Error\n"));
  bool thrown = false;
  try {
    const HighsEnv env{options};
  } catch (const dreamor::highs::HighsCallError& error) {
    thrown = std::string{error.call()} == "passOptions" && error.status() == HighsStatus::kError &&
             std::string{error.what()} == "dreamor::highs: Highs::passOptions returned Error";
  }
  DREAMOR_CHECK(thrown);

  // A model HiGHS refuses throws from the first reach, the call passModel's.
  HighsEnv refused{quietOptions()};
  static_cast<void>(refused.addVariable(std::numeric_limits<double>::quiet_NaN(), 1));
  bool passModel = false;
  try {
    static_cast<void>(refused.highs());
  } catch (const dreamor::highs::HighsCallError& error) {
    passModel = std::string{error.call()} == "passModel" && error.status() == HighsStatus::kError;
  }
  DREAMOR_CHECK(passModel);
}

}  // namespace

int main() try {
  const Plant p           = plant();
  const BomTable quantity = bomTable(p);
  giveOptions(p, quantity);
  reach(p, quantity);
  ownApi(p, quantity);
  failingCall(p, quantity);
  return ::report("guide/backends");
} catch (...) { return ::uncaught(); }
