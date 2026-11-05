// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Multi-machine, multi-period lot sizing - the example model of the library. This file holds
// what does not depend on a solver: the columns and the sets, on the data and instance generator
// of production_planning_data.hpp. production_planning.hpp (Concert), production_planning_mathopt.hpp
// (MathOpt) and production_planning_highs.hpp (HiGHS) each build the same model on these sets.
//
//   sets    ROUTES  = {m in MACHINES, i in ITEMS[m]}            which machine can make what
//           PRODUCE = {(m,i) in ROUTES, t in PERIODS}
//           STOCK   = {i in ITEMS, t in PERIODS}
//   vars    Yield[m,i,t] >= 0, <= cap[m] / time[i]              quantity made
//           Setup[m,i,t] binary                                  setup
//           Stock[i,t]   >= 0                                    stock at the end of t
//   min     sum {(i,t) in STOCK} hold[i] * Stock[i,t] + sum {(m,i,t) in PRODUCE} setup[m] * Setup[m,i,t]
//   s.t.    balance  {(i,t) in STOCK}:   Stock[i,prev(t)] + sum {m: (m,i,t) in PRODUCE} Yield[m,i,t] - demand[i,t] =
//   Stock[i,t]
//           capacity {(m,t)}:            sum {i: (m,i,t) in PRODUCE} time[i] * Yield[m,i,t] <= cap[m]
//           setup    {(m,i,t) in PRODUCE}: Yield[m,i,t] <= ub(Yield[m,i,t]) * Setup[m,i,t]
//
//   Naming: sets sSCREAMING_SNAKE, variable families vPascalCase (the AMPL name with a prefix);
//   callables that take columns snake_case, everything else camelCase.
#pragma once

#include <cstddef>

#include "dreamor/index_set.hpp"
#include "production_planning_data.hpp"

namespace planning {
using cMachine = dreamor::Col<const Machine*, "machine">;
using cItem    = dreamor::Col<const Item*, "item">;
using cPeriod  = dreamor::Col<std::size_t, "period">;

// Sets first (a base class is initialized before the members that use it), then variables.
struct Sets {
  dreamor::IndexSet<cPeriod> sPERIODS;
  dreamor::IndexSet<cMachine, cItem> sROUTES;
  dreamor::IndexSet<cMachine, cItem, cPeriod> sPRODUCE;
  dreamor::IndexSet<cItem, cPeriod> sSTOCK;

  explicit Sets(const Instance& in) {
    const auto items_of = [](cMachine m) { return m->items; };

    sPERIODS = dreamor::makeSet<cPeriod>(in.periods).build();
    sROUTES  = dreamor::makeSet<cMachine, cItem>(in.machines, items_of).build();
    sPRODUCE = dreamor::makeSet<cPeriod>(sROUTES, in.periods).build();
    sSTOCK   = dreamor::makeSet<cItem, cPeriod>(in.items, in.periods).build();
  }
};
}  // namespace planning
