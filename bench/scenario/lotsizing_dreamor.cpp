// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// scenario/lotsizing_dreamor.cpp - lot sizing's dreamor arm, which lotsizing.hpp describes.
//
// The arm is the example itself. build.sh builds it as ./lotsizing_<backend>, against dreamor's headers.
#include <optional>
#include <string>

#include "../common/result.hpp"
#include "backend_dreamor.hpp"
#include "lotsizing.hpp"
#if defined(SCENARIO_MATHOPT)
#  include "../../examples/production_planning_mathopt.hpp"
#elif defined(SCENARIO_HIGHS)
#  include "../../examples/production_planning_highs.hpp"
#else
#  include "../../examples/production_planning_concert.hpp"
#endif

namespace {

#if defined(SCENARIO_MATHOPT)

void runDreamor(const Instance& in, Result& r) {
  math_opt::Model model;
  std::optional<planning::mathopt::Model> m;
  r.measure("variables", [&] { m.emplace(model, in); });
  r.measure("constraints", [&] { m->build(model); });
  solve(model, r);
}

#elif defined(SCENARIO_HIGHS)

void runDreamor(const Instance& in, Result& r) {
  dreamor::highs::HighsEnv env{startOptions()};
  std::optional<planning::highs::Model> m;
  r.measure("variables", [&] { m.emplace(env, in); });
  r.measure("constraints", [&] { m->build(env); });
  solve(env, r);
}

#else  // The backend is Concert: real CPLEX or the stub.

void runDreamor(const Instance& in, Result& r) {
  IloEnv env;
  IloModel model(env);
  std::optional<planning::concert::Model> m;
  r.measure("variables", [&] { m.emplace(env, in); });
  r.measure("constraints", [&] { m->build(env, model); });
  solve(env, model, r);
  env.end();
}

#endif

// Runs the binary's one arm. The hand-written arms are in ./lotsizing_<backend>_hand.
void runArm(const std::string& impl, const Instance& in, Result& r) {
  if (impl == "dreamor") {
    runDreamor(in, r);
  } else {
    r.skip("no arm " + impl + " in ./lotsizing_" + kBackend + ": the hand-written arms are ./lotsizing_" + kBackend +
           "_hand");
  }
}

}  // namespace

int main(int argc, char** argv) {
  scenarioMain(argc, argv, runArm);
}
