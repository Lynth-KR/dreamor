// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// scenario/lotsizing.hpp - the repository's lot-sizing example, built up to the moment a solver starts on it.
//
// One process builds one arm at one size for one repetition, and prints one JSON line. run.py designs the runs.
//
// The model and its data are those of examples/production_planning*.hpp, unchanged. M machines each have a route to
// about 60% of I items, over T periods. The variables are these:
//   - Yield and Setup, on every (machine, item, period) that the routes have;
//   - Stock, on every (item, period).
// The constraints are a stock balance for each (item, period), a capacity for each (machine, period), and a setup link
// for each production row. The objective is the cost of holding stock and of the setups. Every variable is named, as
// the example names them.
//
// Each arm has the same phases:
//   variables    The entities and sets, or what an arm keeps in their place, and the three families of variables.
//   constraints  Every constraint and the objective, handed to the model.
//   extract      The model, handed to the solver.
//   solve0       A solve with a time limit of zero.
// The stub has no solver, so a run against it stops after constraints.
//
// The arms:
//   dreamor        planning::concert::Model, planning::mathopt::Model or planning::highs::Model: the example itself.
//   plain          planning::PlainModel, the example's own comparison. It keeps std::maps by the entities and loops
//                  over the data, as the model is usually written without index sets. As in the example, it runs on
//                  Concert only.
//   flat           Hand-optimized code. The routes are in machine order, and each item's routes are in a CSR. The
//                  variables are in std::vectors by position, and the names are written with std::to_chars. On
//                  HiGHS, the model's arrays are filled by position and handed over in one passModel.
//   callable       CPLEX's Callable Library, the C API under Concert, on CPLEX only. The arrays are filled by position
//                  and handed over in one CPXnewcols and one CPXaddrows.
//   callable-rows  The same columns in one CPXnewcols, then one CPXaddrows for each row, on CPLEX only.
//
// This header holds what the two binaries share, so it includes no dreamor header: the instance, the arguments,
// main's skeleton and the JSON line. lotsizing_dreamor.cpp holds the dreamor arm, ./lotsizing_<backend>.
// lotsizing_hand.cpp holds the hand-written arms, ./lotsizing_<backend>_hand. Each main hands scenarioMain the arms of
// its binary.
//
//   lotsizing --impl dreamor|plain|flat|callable|callable-rows --items <I> [--machines 20] [--periods 50] [--rep r]
//             [--seed s] [--heap-seed h] [--pmu <events>]
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "../../examples/production_planning_data.hpp"
#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "backend.hpp"

namespace {

using planning::Instance;

[[noreturn]] void usage(const std::string& why) {
  std::fprintf(stderr, "%s\n", why.c_str());
  std::exit(2);
}

// main's skeleton. It reads the arguments, makes the instance, calls run(impl, instance, r), and prints the JSON line.
// run is the binary's arms: it runs impl if the binary has that arm, and records a skip otherwise.
[[noreturn]] void scenarioMain(int argc,
                               char** argv,
                               void (&run)(const std::string& impl, const Instance& in, Result& r)) {
  const auto started = Clk::now();
  std::string impl, pmu_spec;
  std::size_t items = 0, machines = 20, periods = 50;
  std::uint32_t rep       = 0;
  std::uint32_t seed      = 7;
  std::uint64_t heap_seed = 0;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--impl") {
      impl = v;
    } else if (k == "--items") {
      items = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--machines") {
      machines = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--periods") {
      periods = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--rep") {
      rep = static_cast<std::uint32_t>(std::stoul(v));
    } else if (k == "--seed") {
      seed = static_cast<std::uint32_t>(std::stoul(v));
    } else if (k == "--heap-seed") {
      heap_seed = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--pmu") {
      pmu_spec = v;
    } else {
      usage("unknown option " + k);
    }
  }
  if (impl != "dreamor" && impl != "plain" && impl != "flat" && impl != "callable" && impl != "callable-rows") {
    usage("--impl dreamor|plain|flat|callable|callable-rows");
  }
  if (items == 0 || machines == 0 || periods == 0) { usage("--items, --machines and --periods must be positive"); }

  Result r;
  std::optional<Instance> in;
  r.setup("heap", [&] { preconditionHeap(heap_seed); });
  r.setup("instance", [&] { in.emplace(Instance::make(machines, items, periods, seed)); });
  const std::size_t routes = in->routes.size();
  if (!pmu_spec.empty()) { pmu().open(pmu_spec); }
  run(impl, *in, r);
  const double main_ms = std::chrono::duration<double, std::milli>(Clk::now() - started).count();

  std::printf(
      "{\"scenario\": \"lotsizing\", \"backend\": \"%s\", \"impl\": \"%s\", \"items\": %zu, \"machines\": %zu, "
      "\"periods\": %zu, \"rows\": %zu, \"rep\": %u, \"seed\": %u, \"heap_seed\": %llu, \"main_ms\": %.3f, "
      "\"peak_rss_bytes\": %lld, %s}\n",
      kBackend,
      impl.c_str(),
      items,
      machines,
      periods,
      routes * periods,
      rep,
      seed,
      static_cast<unsigned long long>(heap_seed),
      main_ms,
      static_cast<long long>(peakBytes()),
      r.json().c_str());
  std::fflush(stdout);
  std::_Exit(0);
}

}  // namespace
