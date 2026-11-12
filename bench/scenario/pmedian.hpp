// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// scenario/pmedian.hpp - the p-median model, built up to the moment a solver starts on it.
//
// One process builds one arm at one size for one repetition, and prints one JSON line. run.py designs the runs.
//
// The model is the one that the modeling-language benchmarks use (Pyomo, Hart et al., MPC 2011; JuMP, Lubin & Dunning,
// IJOC 2015; MathOptInterface, Legat et al., IJOC 2022). C customers are each at one of L candidate locations, drawn
// at random, and P of the locations are opened.
//   min  sum {l, c} |at(c) - l| x[l,c]
//   s.t. sum {l} x[l,c] = 1     for every customer c      (assign)
//        x[l,c] <= y[l]         for every location l and c (link)
//        sum {l} y[l] = P                                   (budget)
//        0 <= x <= 1, y binary
// Each arm has the same phases:
//   sets       dreamor's sets. The hand-written arms time this phase empty.
//   vars       The variables y and x.
//   objective  The objective, handed to the model.
//   assign     One constraint for each customer.
//   link       One constraint for each location and customer: L x C constraints, as many as there are x.
//   budget     The one constraint over y.
//   extract    The model, handed to the solver: IloCplex::extract, MathOpt's NewIncrementalSolver, or HiGHS's
//              passModel.
//   solve0     A solve with a time limit of zero, where the benchmarks stop the clock.
// The stub (bench/common/stub) has no solver, so a run against it stops after budget.
//
// The arms:
//   dreamor        dreamor's sets in set-builder notation, VarTables and sumExpr.
//   hand           The model written straight against the solver's API, as the benchmarks' C++ baselines are. The
//                  variables are in std::vectors by position, and the sums are loops. On HiGHS, whose API takes a
//                  model as arrays, the arrays are filled by position and handed over in one passModel.
//   calls          On HiGHS only: the same arrays a row at a time, each row handed to the Highs as it is made, by
//                  addRow.
//   callable       On CPLEX only, its Callable Library, the C API under Concert: the arrays handed over in one
//                  CPXnewcols and one CPXaddrows.
//   callable-rows  On CPLEX only: the columns in two CPXnewcols, then one CPXaddrows for each row.
//
// This header holds what the two binaries share, so it includes no dreamor header: the instance, the arguments,
// main's skeleton and the JSON line. pmedian_dreamor.cpp holds the dreamor arm, ./pmedian_<backend>. pmedian_hand.cpp
// holds the hand-written arms, ./pmedian_<backend>_hand. Each main hands scenarioMain the arms of its binary.
//
//   pmedian --impl dreamor|hand|calls|callable|callable-rows --L <locations> [--C 100] [--P 100] [--rep r] [--seed s]
//           [--heap-seed h] [--pmu <events>]
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "backend.hpp"

namespace {

struct Location {
  std::uint32_t id;
};

struct Customer {
  // at is the location at which the customer is.
  std::uint32_t id, at;
};

double distance(std::uint32_t location, std::uint32_t at) {
  return location > at ? location - at : at - location;
}

// The entities, in the container that an arm reads them from. Entities<T> is an EntityVector in the dreamor arm,
// because a Ref refers into one, and a std::vector in the hand-written arms. Both hold the entities in one array, read
// by index. Each arm's source file defines Entities, and the add(entities, entity) and seal(entities) that
// makeInstance calls.
template <template <typename> class Entities>
struct BasicInstance {
  Entities<Location> locations;
  Entities<Customer> customers;
  std::size_t p = 0;
};

template <template <typename> class Entities>
BasicInstance<Entities> makeInstance(std::size_t l, std::size_t c, std::size_t p, std::uint64_t seed) {
  std::mt19937_64 g(seed);
  BasicInstance<Entities> in;
  in.p = p;
  in.locations.reserve(l);
  for (std::uint32_t i = 0; i < l; ++i) {
    add(in.locations, Location{i});
  }
  in.customers.reserve(c);
  for (std::uint32_t i = 0; i < c; ++i) {
    add(in.customers, Customer{i, static_cast<std::uint32_t>(g() % l)});
  }
  seal(in.locations);
  seal(in.customers);
  return in;
}

[[noreturn]] void usage(const std::string& why) {
  std::fprintf(stderr, "%s\n", why.c_str());
  std::exit(2);
}

// main's skeleton. It reads the arguments, makes the instance, calls run(impl, instance, r), and prints the JSON line.
// run is the binary's arms: it runs impl if the binary has that arm, and records a skip otherwise.
template <template <typename> class Entities>
[[noreturn]] void scenarioMain(int argc,
                               char** argv,
                               void (&run)(const std::string& impl, const BasicInstance<Entities>& in, Result& r)) {
  const auto started = Clk::now();
  std::string impl, pmu_spec;
  std::size_t l = 0, c = 100, p = 100;
  std::uint32_t rep       = 0;
  std::uint64_t seed      = 10;
  std::uint64_t heap_seed = 0;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--impl") {
      impl = v;
    } else if (k == "--L") {
      l = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--C") {
      c = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--P") {
      p = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--rep") {
      rep = static_cast<std::uint32_t>(std::stoul(v));
    } else if (k == "--seed") {
      seed = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--heap-seed") {
      heap_seed = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--pmu") {
      pmu_spec = v;
    } else {
      usage("unknown option " + k);
    }
  }
  if (impl != "dreamor" && impl != "hand" && impl != "calls" && impl != "callable" && impl != "callable-rows") {
    usage("--impl dreamor|hand|calls|callable|callable-rows");
  }
  if (l == 0 || c == 0 || p > l) { usage("--L, --C must be positive and --P at most --L"); }

  Result r;
  BasicInstance<Entities> in;
  r.setup("heap", [&] { preconditionHeap(heap_seed); });
  r.setup("instance", [&] { in = makeInstance<Entities>(l, c, p, seed); });
  if (!pmu_spec.empty()) { pmu().open(pmu_spec); }
  run(impl, in, r);
  const double main_ms = std::chrono::duration<double, std::milli>(Clk::now() - started).count();

  std::printf(
      "{\"scenario\": \"pmedian\", \"backend\": \"%s\", \"impl\": \"%s\", \"L\": %zu, \"C\": %zu, \"P\": %zu, "
      "\"rep\": %u, \"seed\": %llu, \"heap_seed\": %llu, \"main_ms\": %.3f, \"peak_rss_bytes\": %lld, %s}\n",
      kBackend,
      impl.c_str(),
      l,
      c,
      p,
      rep,
      static_cast<unsigned long long>(seed),
      static_cast<unsigned long long>(heap_seed),
      main_ms,
      static_cast<long long>(peakBytes()),
      r.json().c_str());
  std::fflush(stdout);
  std::_Exit(0);
}

}  // namespace
