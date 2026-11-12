// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// scenario/pmedian_dreamor.cpp - p-median's dreamor arm, which pmedian.hpp describes.
//
// The arm writes the model with dreamor's sets, VarTables and sumExpr. build.sh builds it as ./pmedian_<backend>,
// against dreamor's headers.
#include <optional>
#include <string>

#include "../common/result.hpp"
#include "backend_dreamor.hpp"
#include "dreamor/index_set.hpp"
#include "pmedian.hpp"

namespace {

std::uint32_t primaryKey(const Location& l) {
  return l.id;
}

std::uint32_t primaryKey(const Customer& c) {
  return c.id;
}

DREAMOR_ENTITY_COLUMN(cLocation, Location);
DREAMOR_ENTITY_COLUMN(cCustomer, Customer);

// The dreamor arm holds the entities in EntityVectors, because a Ref refers into one.
template <typename T>
using Entities = dreamor::EntityVector<T>;

template <typename T>
void add(Entities<T>& entities, const T& entity) {
  static_cast<void>(entities.insert(entity));
}

template <typename T>
void seal(Entities<T>& entities) {
  entities.freeze();
}

using Instance = BasicInstance<Entities>;

#if defined(SCENARIO_MATHOPT)

void runDreamor(const Instance& instance, Result& r) {
  using namespace dreamor::index_set::set_builder;
  using dreamor::mathopt::sumExpr;
  math_opt::Model model;
  // A set has no empty state, so each set is built in an optional inside the phase that times it, and then read by
  // name.
  std::optional<dreamor::IndexSet<cLocation>> builtLocations;
  std::optional<dreamor::IndexSet<cCustomer>> builtCustomers;
  std::optional<dreamor::IndexSet<cLocation, cCustomer>> builtAssign;
  r.measure("sets", [&] {
    builtLocations.emplace(of<cLocation>(in<cLocation>(instance.locations)).build());
    builtCustomers.emplace(of<cCustomer>(in<cCustomer>(instance.customers)).build());
    builtAssign.emplace(
        of<cLocation, cCustomer>(in<cLocation>(instance.locations), in<cCustomer>(instance.customers)).build());
  });
  const auto& sLOCATIONS = *builtLocations;
  const auto& sCUSTOMERS = *builtCustomers;
  const auto& sASSIGN    = *builtAssign;
  std::optional<dreamor::mathopt::VarTable<cLocation>> vY;
  std::optional<dreamor::mathopt::VarTable<cLocation, cCustomer>> vX;
  r.measure("vars", [&] {
    vY.emplace(model, sLOCATIONS, 0, 1, true);
    vX.emplace(model, sASSIGN, 0, 1);
  });
  r.measure("objective", [&] {
    const auto assign_cost = [&](cLocation l, cCustomer c) { return distance(l->id, c->at) * (*vX)(l, c); };
    model.Minimize(sumExpr(sASSIGN, assign_cost).build());
  });
  r.measure("assign", [&] {
    const auto assignedTo = sASSIGN.groupBy<cCustomer>();
    for (const auto& [customer] : sCUSTOMERS) {
      model.AddLinearConstraint(sumExpr(assignedTo.slice(customer), *vX).build() == 1.0);
    }
  });
  r.measure("link", [&] {
    for (const auto& [location, customer] : sASSIGN) {
      model.AddLinearConstraint((*vX)(location, customer) <= (*vY)(location));
    }
  });
  r.measure("budget",
            [&] { model.AddLinearConstraint(sumExpr(sLOCATIONS, *vY).build() == static_cast<double>(instance.p)); });
  solve(model, r);
}

#elif defined(SCENARIO_HIGHS)

void runDreamor(const Instance& instance, Result& r) {
  using namespace dreamor::index_set::set_builder;
  namespace hs = dreamor::highs;
  hs::HighsEnv env{startOptions()};
  // A set has no empty state, so each set is built in an optional inside the phase that times it, and then read by
  // name.
  std::optional<dreamor::IndexSet<cLocation>> builtLocations;
  std::optional<dreamor::IndexSet<cCustomer>> builtCustomers;
  std::optional<dreamor::IndexSet<cLocation, cCustomer>> builtAssign;
  r.measure("sets", [&] {
    builtLocations.emplace(of<cLocation>(in<cLocation>(instance.locations)).build());
    builtCustomers.emplace(of<cCustomer>(in<cCustomer>(instance.customers)).build());
    builtAssign.emplace(
        of<cLocation, cCustomer>(in<cLocation>(instance.locations), in<cCustomer>(instance.customers)).build());
  });
  const auto& sLOCATIONS = *builtLocations;
  const auto& sCUSTOMERS = *builtCustomers;
  const auto& sASSIGN    = *builtAssign;
  std::optional<hs::VarTable<cLocation>> vY;
  std::optional<hs::VarTable<cLocation, cCustomer>> vX;
  r.measure("vars", [&] {
    vY.emplace(env, sLOCATIONS, 0, 1, HighsVarType::kInteger);
    vX.emplace(env, sASSIGN, 0, 1);
  });
  r.measure("objective", [&] {
    const auto assign_cost = [&](cLocation l, cCustomer c) { return distance(l->id, c->at) * (*vX)(l, c); };
    hs::setObjective(env, hs::sumExpr(sASSIGN, assign_cost).build());
  });
  r.measure("assign", [&] {
    const auto assignedTo = sASSIGN.groupBy<cCustomer>();
    for (const auto& [customer] : sCUSTOMERS) {
      hs::addConstr(env, hs::sumExpr(assignedTo.slice(customer), *vX).build() == 1.0);
    }
  });
  r.measure("link", [&] {
    for (const auto& [location, customer] : sASSIGN) {
      hs::addConstr(env, (*vX)(location, customer) <= (*vY)(location));
    }
  });
  r.measure("budget",
            [&] { hs::addConstr(env, hs::sumExpr(sLOCATIONS, *vY).build() == static_cast<double>(instance.p)); });
  solve(env, r);
}

#else  // The backend is Concert: real CPLEX or the stub.

void runDreamor(const Instance& instance, Result& r) {
  using namespace dreamor::index_set::set_builder;
  using dreamor::concert::sumExpr;
  IloEnv env;
  IloModel model(env);
  // A set has no empty state, so each set is built in an optional inside the phase that times it, and then read by
  // name.
  std::optional<dreamor::IndexSet<cLocation>> builtLocations;
  std::optional<dreamor::IndexSet<cCustomer>> builtCustomers;
  std::optional<dreamor::IndexSet<cLocation, cCustomer>> builtAssign;
  r.measure("sets", [&] {
    builtLocations.emplace(of<cLocation>(in<cLocation>(instance.locations)).build());
    builtCustomers.emplace(of<cCustomer>(in<cCustomer>(instance.customers)).build());
    builtAssign.emplace(
        of<cLocation, cCustomer>(in<cLocation>(instance.locations), in<cCustomer>(instance.customers)).build());
  });
  const auto& sLOCATIONS = *builtLocations;
  const auto& sCUSTOMERS = *builtCustomers;
  const auto& sASSIGN    = *builtAssign;
  std::optional<dreamor::concert::VarTable<cLocation>> vY;
  std::optional<dreamor::concert::VarTable<cLocation, cCustomer>> vX;
  r.measure("vars", [&] {
    vY.emplace(env, sLOCATIONS, 0, 1, ILOBOOL);
    vX.emplace(env, sASSIGN, 0, 1, ILOFLOAT);
  });
  r.measure("objective", [&] {
    const auto assign_cost = [&](cLocation l, cCustomer c) { return distance(l->id, c->at) * (*vX)(l, c); };
    IloExpr objective      = sumExpr(env, sASSIGN, assign_cost).build();
    model.add(IloMinimize(env, objective));
    objective.end();
  });
  r.measure("assign", [&] {
    const auto assignedTo = sASSIGN.groupBy<cCustomer>();
    for (const auto& [customer] : sCUSTOMERS) {
      IloExpr assigned = sumExpr(env, assignedTo.slice(customer), *vX).build();
      model.add(assigned == 1);
      assigned.end();
    }
  });
  r.measure("link", [&] {
    for (const auto& [location, customer] : sASSIGN) {
      model.add((*vX)(location, customer) <= (*vY)(location));
    }
  });
  r.measure("budget", [&] {
    IloExpr opened = sumExpr(env, sLOCATIONS, *vY).build();
    model.add(opened == static_cast<IloNum>(instance.p));
    opened.end();
  });
  solve(env, model, r);
  env.end();
}

#endif

// Runs the binary's one arm. The hand-written arms are in ./pmedian_<backend>_hand.
void runArm(const std::string& impl, const Instance& in, Result& r) {
  if (impl == "dreamor") {
    runDreamor(in, r);
  } else {
    r.skip("no arm " + impl + " in ./pmedian_" + kBackend + ": the hand-written arms are ./pmedian_" + kBackend +
           "_hand");
  }
}

}  // namespace

int main(int argc, char** argv) {
  scenarioMain<Entities>(argc, argv, runArm);
}
