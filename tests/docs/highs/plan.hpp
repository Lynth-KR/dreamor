// SPDX-License-Identifier: MIT-0
//
// The guide's model on HiGHS: the plan's variables, its rows and its cost over the model's sets - what the pages on
// variables, sums, constraints and solutions stand on.
#pragma once

#include "Highs.h"
#include "docs/guide/model.hpp"
#include "dreamor/highs.hpp"

namespace guide::highs {

using namespace dreamor::highs;

// --8<-- [start:plan]
struct Plan {
  static constexpr auto max_make = [](cMachine m, cItem i) { return m->capacity / i->time; };

  Plan(const Sets& s, const BomTable& quantity, const HighsOptions& options)
      : env{options},
        vMake{env, s.produce, 0, max_make, HighsVarType::kContinuous, "Make"},
        vSetup{env, s.produce, 0, 1, HighsVarType::kInteger, "Setup"},
        vStock{env, s.stock, 0, kHighsInf, HighsVarType::kContinuous, "Stock"} {
    addBalance(s, quantity);
    addHours(s);
    addSetups(s);
    setCost(s);
  }

  HighsEnv env;
  const VarTable<cMachine, cItem, cPeriod> vMake;   // how much a machine makes of an item in a period
  const VarTable<cMachine, cItem, cPeriod> vSetup;  // whether it is set up for that item then, 0 or 1
  const VarTable<cItem, cPeriod> vStock;            // how much of an item is kept at a period's end

  // --8<-- [end:plan]

 private:
  // --8<-- [start:balance]
  // Stock carried in + made - what customers take - what the parents made take = stock carried out
  void addBalance(const Sets& s, const BomTable& quantity) {
    const auto madeByItemPeriod = s.produce.groupBy<cItem, cPeriod>();
    const auto takenByChild     = s.consume.groupBy<cChild, cPeriod>();
    const auto taken = [&](cParent a, cChild b, cMachine m, cPeriod t) { return quantity(a, b) * vMake(m, a, t); };
    const auto orderedPeriods = s.periods.ordered();
    for (const auto& [i, t] : s.stock) {
      HighsLinearExpression inflow = sumExpr(madeByItemPeriod.slice(i, t), vMake).build();
      if (const auto* before = orderedPeriods.prev(t)) { inflow += vStock(i, *before); }
      const auto used = sumExpr(takenByChild.slice(i, t), taken).build();
      addConstr(env, inflow - used - vStock(i, t) == i->demand[t - 1], dreamor::indexedName("Balance", i, t));
    }
  }

  // --8<-- [end:balance]

  // --8<-- [start:hours]
  // The hours each machine works in a period, up to its capacity
  void addHours(const Sets& s) {
    const auto hours = [this](cMachine m, cItem i, cPeriod t) { return i->time * vMake(m, i, t); };
    for (const auto& [m, t, rows] : s.produce.groupBy<cMachine, cPeriod>()) {
      addConstr(env, sumExpr(rows, hours).build() <= m->capacity, dreamor::indexedName("Hours", m, t));
    }
  }

  // --8<-- [end:hours]

  // --8<-- [start:setups]
  // Nothing is made without a setup
  void addSetups(const Sets& s) {
    for (const auto& [m, i, t] : s.produce) {
      addConstr(env, vMake(m, i, t) <= max_make(m, i) * vSetup(m, i, t), dreamor::indexedName("Setup", m, i, t));
    }
  }

  // --8<-- [end:setups]

  // --8<-- [start:cost]
  // Holding stock and setting up, to be made least
  void setCost(const Sets& s) {
    const auto holding = [this](cItem i, cPeriod t) { return i->hold * vStock(i, t); };
    const auto setups  = [this](cMachine m, cItem i, cPeriod t) { return m->setup * vSetup(m, i, t); };
    setObjective(env, sumExpr(s.stock, holding).build() + sumExpr(s.produce, setups).build(), ObjSense::kMinimize);
  }

  // --8<-- [end:cost]
};

}  // namespace guide::highs
