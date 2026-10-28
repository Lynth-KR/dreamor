// SPDX-License-Identifier: Apache-2.0
// scenario/pmedian.cpp - the p-median model, built up to the moment a solver starts on it: one arm,
// one size, one repetition, in one process; prints one JSON line. run.py designs the runs.
//
// The model of the modeling-language benchmarks (Pyomo, Hart et al., MPC 2011; JuMP, Lubin &
// Dunning, IJOC 2015; MathOptInterface, Legat et al., IJOC 2022): C customers, each at one of L
// candidate locations drawn at random, P of the locations to open.
//   min  sum {l, c} |at(c) - l| x[l,c]
//   s.t. sum {l} x[l,c] = 1     for every customer c      (assign)
//        x[l,c] <= y[l]         for every location l and c (link)
//        sum {l} y[l] = P                                   (budget)
//        0 <= x <= 1, y binary
// Phases, the same in each arm:
//   sets       dreamor's sets (none in the hand-written arm)
//   vars       y and x
//   objective  the objective, handed to the model
//   assign     a constraint a customer
//   link       a constraint a location and customer: L x C of them, as many as x
//   budget     the one constraint over y
//   extract    the model handed to the solver (IloCplex::extract, MathOpt's NewIncrementalSolver)
//   solve0     a solve with a time limit of zero: where the benchmarks stop the clock
// Against the stub (bench/common/stub) there is no solver: the run stops after budget.
//
// Arms: dreamor's sets, VarTables and sumOf, written as the README writes a model; and the model
// written straight against the solver's API, as the benchmarks' C++ baselines are (`hand`):
// variables in std::vectors indexed by position, the sums as loops.
//
//   pmedian --impl dreamor|hand --L <locations> [--C 100] [--P 100] [--rep r] [--seed s]
//           [--heap-seed h] [--pmu <events>]
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "backend.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/logger/logger_light.hpp"

namespace {

struct Location {
  std::uint32_t id;
};
struct Customer {
  std::uint32_t id, at;  // at: the location it is at
};
std::uint32_t keyName(const Location* l) {
  return l->id;
}
std::uint32_t keyName(const Customer* c) {
  return c->id;
}
using LocationCol = dreamor::Col<const Location*, "location">;
using CustomerCol = dreamor::Col<const Customer*, "customer">;

double distance(std::uint32_t location, std::uint32_t at) {
  return location > at ? location - at : at - location;
}

struct Instance {
  std::vector<Location> locations;
  std::vector<Customer> customers;
  std::size_t p = 0;
};

Instance makeInstance(std::size_t l, std::size_t c, std::size_t p, std::uint64_t seed) {
  std::mt19937_64 g(seed);
  Instance in;
  in.p = p;
  in.locations.reserve(l);
  for (std::uint32_t i = 0; i < l; ++i) {
    in.locations.push_back({i});
  }
  in.customers.reserve(c);
  for (std::uint32_t i = 0; i < c; ++i) {
    in.customers.push_back({i, static_cast<std::uint32_t>(g() % l)});
  }
  return in;
}

#if defined(SCENARIO_MATHOPT)

void runDreamor(const Instance& in, Result& r) {
  using dreamor::mathopt::sumOf;
  math_opt::Model model;
  dreamor::SetOf<LocationCol> sLOCATIONS;
  dreamor::SetOf<CustomerCol> sCUSTOMERS;
  dreamor::SetOf<LocationCol, CustomerCol> sASSIGN;
  r.measure("sets", [&] {
    sLOCATIONS = dreamor::makeSet<LocationCol>(in.locations).collect();
    sCUSTOMERS = dreamor::makeSet<CustomerCol>(in.customers).collect();
    sASSIGN    = dreamor::makeSet<LocationCol, CustomerCol>(in.locations, in.customers).collect();
  });
  std::optional<dreamor::mathopt::VarTable<LocationCol>> vY;
  std::optional<dreamor::mathopt::VarTable<LocationCol, CustomerCol>> vX;
  r.measure("vars", [&] {
    vY.emplace(model, sLOCATIONS, 0, 1, true);
    vX.emplace(model, sASSIGN, 0, 1);
  });
  r.measure("objective", [&] {
    model.Minimize(
        sumOf(sASSIGN, [&](LocationCol l, CustomerCol c) { return distance(l->id, c->at) * (*vX)(l, c); }).build());
  });
  r.measure("assign", [&] {
    const auto assignedTo = sASSIGN.groupBy<CustomerCol>();
    for (const auto& [customer] : sCUSTOMERS) {
      model.AddLinearConstraint(sumOf(assignedTo.slice(customer), *vX).build() == 1.0);
    }
  });
  r.measure("link", [&] {
    for (const auto& [location, customer] : sASSIGN) {
      model.AddLinearConstraint((*vX)(location, customer) <= (*vY)(location));
    }
  });
  r.measure("budget", [&] { model.AddLinearConstraint(sumOf(sLOCATIONS, *vY).build() == static_cast<double>(in.p)); });
  solve(model, r);
}

void runHand(const Instance& in, Result& r) {
  const std::size_t l_count = in.locations.size(), c_count = in.customers.size();
  math_opt::Model model;
  std::vector<math_opt::Variable> y, x;  // x[l * C + c]
  r.measure("sets", [] {});
  r.measure("vars", [&] {
    y.reserve(l_count);
    for (std::size_t l = 0; l < l_count; ++l) {
      y.push_back(model.AddBinaryVariable());
    }
    x.reserve(l_count * c_count);
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        x.push_back(model.AddContinuousVariable(0, 1));
      }
    }
  });
  r.measure("objective", [&] {
    math_opt::LinearExpression objective;
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        objective += distance(static_cast<std::uint32_t>(l), in.customers[c].at) * x[l * c_count + c];
      }
    }
    model.Minimize(std::move(objective));
  });
  r.measure("assign", [&] {
    for (std::size_t c = 0; c < c_count; ++c) {
      math_opt::LinearExpression assigned;
      for (std::size_t l = 0; l < l_count; ++l) {
        assigned += x[l * c_count + c];
      }
      model.AddLinearConstraint(std::move(assigned) == 1.0);
    }
  });
  r.measure("link", [&] {
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        model.AddLinearConstraint(x[l * c_count + c] <= y[l]);
      }
    }
  });
  r.measure("budget", [&] {
    math_opt::LinearExpression opened;
    for (const math_opt::Variable& v : y) {
      opened += v;
    }
    model.AddLinearConstraint(std::move(opened) == static_cast<double>(in.p));
  });
  solve(model, r);
}

#else  // Concert, real or the stub

void runDreamor(const Instance& in, Result& r) {
  using dreamor::concert::sumOf;
  IloEnv env;
  IloModel model(env);
  dreamor::SetOf<LocationCol> sLOCATIONS;
  dreamor::SetOf<CustomerCol> sCUSTOMERS;
  dreamor::SetOf<LocationCol, CustomerCol> sASSIGN;
  r.measure("sets", [&] {
    sLOCATIONS = dreamor::makeSet<LocationCol>(in.locations).collect();
    sCUSTOMERS = dreamor::makeSet<CustomerCol>(in.customers).collect();
    sASSIGN    = dreamor::makeSet<LocationCol, CustomerCol>(in.locations, in.customers).collect();
  });
  std::optional<dreamor::concert::VarTable<LocationCol>> vY;
  std::optional<dreamor::concert::VarTable<LocationCol, CustomerCol>> vX;
  r.measure("vars", [&] {
    vY.emplace(env, sLOCATIONS, 0, 1, ILOBOOL);
    vX.emplace(env, sASSIGN, 0, 1, ILOFLOAT);
  });
  r.measure("objective", [&] {
    IloExpr objective =
        sumOf(env, sASSIGN, [&](LocationCol l, CustomerCol c) { return distance(l->id, c->at) * (*vX)(l, c); }).build();
    model.add(IloMinimize(env, objective));
    objective.end();
  });
  r.measure("assign", [&] {
    const auto assignedTo = sASSIGN.groupBy<CustomerCol>();
    for (const auto& [customer] : sCUSTOMERS) {
      IloExpr assigned = sumOf(env, assignedTo.slice(customer), *vX).build();
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
    IloExpr opened = sumOf(env, sLOCATIONS, *vY).build();
    model.add(opened == static_cast<IloNum>(in.p));
    opened.end();
  });
  solve(env, model, r);
  env.end();
}

void runHand(const Instance& in, Result& r) {
  const std::size_t l_count = in.locations.size(), c_count = in.customers.size();
  IloEnv env;
  IloModel model(env);
  std::vector<IloNumVar> y, x;  // x[l * C + c]
  r.measure("sets", [] {});
  r.measure("vars", [&] {
    y.reserve(l_count);
    for (std::size_t l = 0; l < l_count; ++l) {
      y.emplace_back(env, 0, 1, ILOBOOL);
    }
    x.reserve(l_count * c_count);
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        x.emplace_back(env, 0, 1, ILOFLOAT);
      }
    }
  });
  r.measure("objective", [&] {
    IloExpr objective(env);
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        objective += distance(static_cast<std::uint32_t>(l), in.customers[c].at) * x[l * c_count + c];
      }
    }
    model.add(IloMinimize(env, objective));
    objective.end();
  });
  r.measure("assign", [&] {
    for (std::size_t c = 0; c < c_count; ++c) {
      IloExpr assigned(env);
      for (std::size_t l = 0; l < l_count; ++l) {
        assigned += x[l * c_count + c];
      }
      model.add(assigned == 1);
      assigned.end();
    }
  });
  r.measure("link", [&] {
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        model.add(x[l * c_count + c] <= y[l]);
      }
    }
  });
  r.measure("budget", [&] {
    IloExpr opened(env);
    for (const IloNumVar& v : y) {
      opened += v;
    }
    model.add(opened == static_cast<IloNum>(in.p));
    opened.end();
  });
  solve(env, model, r);
  env.end();
}
#endif

[[noreturn]] void usage(const std::string& why) {
  DREAMOR_LOG_ERROR("{}", why);
  std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
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
  if (impl != "dreamor" && impl != "hand") { usage("--impl dreamor|hand"); }
  if (l == 0 || c == 0 || p > l) { usage("--L, --C must be positive and --P at most --L"); }

  Result r;
  Instance in;
  r.setup("heap", [&] { preconditionHeap(heap_seed); });
  r.setup("instance", [&] { in = makeInstance(l, c, p, seed); });
  if (!pmu_spec.empty()) { pmu().open(pmu_spec); }
  if (impl == "dreamor") {
    runDreamor(in, r);
  } else {
    runHand(in, r);
  }
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
