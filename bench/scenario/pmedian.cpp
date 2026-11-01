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
//   extract    the model handed to the solver (IloCplex::extract, MathOpt's NewIncrementalSolver,
//              HiGHS's passModel)
//   solve0     a solve with a time limit of zero: where the benchmarks stop the clock
// Against the stub (bench/common/stub) there is no solver: the run stops after budget.
//
// Arms: dreamor's sets, VarTables and sumOf, written as the README writes a model; and the model
// written straight against the solver's API, as the benchmarks' C++ baselines are (`hand`):
// variables in std::vectors indexed by position, the sums as loops - on HiGHS, whose API takes a
// model as arrays, the arrays filled by position and handed over in one passModel. On HiGHS alone,
// `calls`: the same arrays a row at a time, each handed to the Highs as it is made (addRow). On
// CPLEX, its Callable Library, the C API under Concert, the same two ways: `callable`, arrays
// handed over in one CPXnewcols and one CPXaddrows, and `callable-rows`, a CPXaddrows a row.
//
//   pmedian --impl dreamor|hand|calls|callable|callable-rows --L <locations> [--C 100] [--P 100] [--rep r] [--seed s]
//           [--heap-seed h] [--pmu <events>]
#include <algorithm>
#include <array>
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
#include "dreamor/logger/logger_light.hpp"
#if SCENARIO_DREAMOR
#  include "dreamor/index_set.hpp"
#endif

namespace {

struct Location {
  std::uint32_t id;
};
struct Customer {
  std::uint32_t id, at;  // at: the location it is at
};
#if SCENARIO_DREAMOR
std::uint32_t keyName(const Location* l) {
  return l->id;
}
std::uint32_t keyName(const Customer* c) {
  return c->id;
}
using cLocation = dreamor::Col<const Location*, "location">;
using cCustomer = dreamor::Col<const Customer*, "customer">;
#endif

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

#  if SCENARIO_DREAMOR

void runDreamor(const Instance& in, Result& r) {
  using dreamor::mathopt::sumOf;
  math_opt::Model model;
  dreamor::SetOf<cLocation> sLOCATIONS;
  dreamor::SetOf<cCustomer> sCUSTOMERS;
  dreamor::SetOf<cLocation, cCustomer> sASSIGN;
  r.measure("sets", [&] {
    sLOCATIONS = dreamor::makeSet<cLocation>(in.locations).collect();
    sCUSTOMERS = dreamor::makeSet<cCustomer>(in.customers).collect();
    sASSIGN    = dreamor::makeSet<cLocation, cCustomer>(in.locations, in.customers).collect();
  });
  std::optional<dreamor::mathopt::VarTable<cLocation>> vY;
  std::optional<dreamor::mathopt::VarTable<cLocation, cCustomer>> vX;
  r.measure("vars", [&] {
    vY.emplace(model, sLOCATIONS, 0, 1, true);
    vX.emplace(model, sASSIGN, 0, 1);
  });
  r.measure("objective", [&] {
    model.Minimize(
        sumOf(sASSIGN, [&](cLocation l, cCustomer c) { return distance(l->id, c->at) * (*vX)(l, c); }).build());
  });
  r.measure("assign", [&] {
    const auto assignedTo = sASSIGN.groupBy<cCustomer>();
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

#  else

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

void runCalls(const Instance& /*in*/, Result& r) {
  r.skip("a call a row is HiGHS's own API");
}

#  endif

#elif defined(SCENARIO_HIGHS)

#  if SCENARIO_DREAMOR

void runDreamor(const Instance& in, Result& r) {
  namespace hs = dreamor::highs;
  hs::HighsEnv env{startOptions()};
  dreamor::SetOf<cLocation> sLOCATIONS;
  dreamor::SetOf<cCustomer> sCUSTOMERS;
  dreamor::SetOf<cLocation, cCustomer> sASSIGN;
  r.measure("sets", [&] {
    sLOCATIONS = dreamor::makeSet<cLocation>(in.locations).collect();
    sCUSTOMERS = dreamor::makeSet<cCustomer>(in.customers).collect();
    sASSIGN    = dreamor::makeSet<cLocation, cCustomer>(in.locations, in.customers).collect();
  });
  std::optional<hs::VarTable<cLocation>> vY;
  std::optional<hs::VarTable<cLocation, cCustomer>> vX;
  r.measure("vars", [&] {
    vY.emplace(env, sLOCATIONS, 0, 1, HighsVarType::kInteger);
    vX.emplace(env, sASSIGN, 0, 1);
  });
  r.measure("objective", [&] {
    hs::setObjective(env, hs::sumOf(sASSIGN, [&](cLocation l, cCustomer c) {
                            return distance(l->id, c->at) * (*vX)(l, c);
                          }).build());
  });
  r.measure("assign", [&] {
    const auto assignedTo = sASSIGN.groupBy<cCustomer>();
    for (const auto& [customer] : sCUSTOMERS) {
      hs::addConstr(env, hs::sumOf(assignedTo.slice(customer), *vX).build() == 1.0);
    }
  });
  r.measure("link", [&] {
    for (const auto& [location, customer] : sASSIGN) {
      hs::addConstr(env, (*vX)(location, customer) <= (*vY)(location));
    }
  });
  r.measure("budget", [&] { hs::addConstr(env, hs::sumOf(sLOCATIONS, *vY).build() == static_cast<double>(in.p)); });
  solve(env, r);
}

#  else

// Columns y[l] first, then x[l * C + c]; rows row-wise, as HighsEnv collects them.
void runHand(const Instance& in, Result& r) {
  const std::size_t l_count = in.locations.size(), c_count = in.customers.size();
  const auto x = [&](std::size_t l, std::size_t c) { return static_cast<HighsInt>(l_count + l * c_count + c); };
  Highs highs;
  highs.passOptions(startOptions());
  HighsModel model;
  HighsLp& lp          = model.lp_;
  HighsSparseMatrix& a = lp.a_matrix_;
  a.format_            = MatrixFormat::kRowwise;
  a.start_.assign(1, 0);
  const auto endRow = [&](double lower, double upper) {
    lp.row_lower_.push_back(lower);
    lp.row_upper_.push_back(upper);
    a.start_.push_back(static_cast<HighsInt>(a.index_.size()));
  };
  r.measure("sets", [] {});
  r.measure("vars", [&] {
    const std::size_t n = l_count + l_count * c_count;
    lp.col_lower_.assign(n, 0);
    lp.col_upper_.assign(n, 1);
    lp.integrality_.assign(n, HighsVarType::kContinuous);
    std::fill_n(lp.integrality_.begin(), l_count, HighsVarType::kInteger);
  });
  r.measure("objective", [&] {
    lp.col_cost_.assign(l_count + l_count * c_count, 0);
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        lp.col_cost_[x(l, c)] = distance(static_cast<std::uint32_t>(l), in.customers[c].at);
      }
    }
  });
  r.measure("assign", [&] {
    for (std::size_t c = 0; c < c_count; ++c) {
      for (std::size_t l = 0; l < l_count; ++l) {
        a.index_.push_back(x(l, c));
        a.value_.push_back(1);
      }
      endRow(1, 1);
    }
  });
  r.measure("link", [&] {
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        a.index_.insert(a.index_.end(), {static_cast<HighsInt>(l), x(l, c)});
        a.value_.insert(a.value_.end(), {-1, 1});
        endRow(-kHighsInf, 0);
      }
    }
  });
  r.measure("budget", [&] {
    for (std::size_t l = 0; l < l_count; ++l) {
      a.index_.push_back(static_cast<HighsInt>(l));
      a.value_.push_back(1);
    }
    endRow(static_cast<double>(in.p), static_cast<double>(in.p));
  });
  lp.num_col_ = a.num_col_ = static_cast<HighsInt>(lp.col_cost_.size());
  lp.num_row_ = a.num_row_ = static_cast<HighsInt>(lp.row_lower_.size());
  solve(highs, std::move(model), r);
}

// HiGHS's own API a call a row, as its examples write a model: the columns in two calls, every row
// handed to the Highs as it is made.
void runCalls(const Instance& in, Result& r) {
  const std::size_t l_count = in.locations.size(), c_count = in.customers.size();
  const auto x = [&](std::size_t l, std::size_t c) { return static_cast<HighsInt>(l_count + l * c_count + c); };
  Highs highs;
  highs.passOptions(startOptions());
  r.measure("sets", [] {});
  r.measure("vars", [&] {
    const std::vector<double> lower(l_count * c_count, 0), upper(l_count * c_count, 1);
    highs.addVars(static_cast<HighsInt>(l_count), lower.data(), upper.data());
    const std::vector<HighsVarType> integer(l_count, HighsVarType::kInteger);
    highs.changeColsIntegrality(0, static_cast<HighsInt>(l_count) - 1, integer.data());
    highs.addVars(static_cast<HighsInt>(l_count * c_count), lower.data(), upper.data());
  });
  r.measure("objective", [&] {
    std::vector<double> cost(l_count * c_count);
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        cost[l * c_count + c] = distance(static_cast<std::uint32_t>(l), in.customers[c].at);
      }
    }
    highs.changeColsCost(x(0, 0), x(l_count - 1, c_count - 1), cost.data());
  });
  r.measure("assign", [&] {
    std::vector<HighsInt> index(l_count);
    const std::vector<double> ones(l_count, 1);
    for (std::size_t c = 0; c < c_count; ++c) {
      for (std::size_t l = 0; l < l_count; ++l) {
        index[l] = x(l, c);
      }
      highs.addRow(1, 1, static_cast<HighsInt>(l_count), index.data(), ones.data());
    }
  });
  r.measure("link", [&] {
    const std::array<double, 2> value{-1, 1};
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        const std::array<HighsInt, 2> index{static_cast<HighsInt>(l), x(l, c)};
        highs.addRow(-kHighsInf, 0, 2, index.data(), value.data());
      }
    }
  });
  r.measure("budget", [&] {
    std::vector<HighsInt> index(l_count);
    for (std::size_t l = 0; l < l_count; ++l) {
      index[l] = static_cast<HighsInt>(l);
    }
    const std::vector<double> ones(l_count, 1);
    highs.addRow(static_cast<double>(in.p),
                 static_cast<double>(in.p),
                 static_cast<HighsInt>(l_count),
                 index.data(),
                 ones.data());
  });
  solve(highs, r);
}

#  endif

#else  // Concert, real or the stub

#  if SCENARIO_DREAMOR

void runDreamor(const Instance& in, Result& r) {
  using dreamor::concert::sumOf;
  IloEnv env;
  IloModel model(env);
  dreamor::SetOf<cLocation> sLOCATIONS;
  dreamor::SetOf<cCustomer> sCUSTOMERS;
  dreamor::SetOf<cLocation, cCustomer> sASSIGN;
  r.measure("sets", [&] {
    sLOCATIONS = dreamor::makeSet<cLocation>(in.locations).collect();
    sCUSTOMERS = dreamor::makeSet<cCustomer>(in.customers).collect();
    sASSIGN    = dreamor::makeSet<cLocation, cCustomer>(in.locations, in.customers).collect();
  });
  std::optional<dreamor::concert::VarTable<cLocation>> vY;
  std::optional<dreamor::concert::VarTable<cLocation, cCustomer>> vX;
  r.measure("vars", [&] {
    vY.emplace(env, sLOCATIONS, 0, 1, ILOBOOL);
    vX.emplace(env, sASSIGN, 0, 1, ILOFLOAT);
  });
  r.measure("objective", [&] {
    IloExpr objective =
        sumOf(env, sASSIGN, [&](cLocation l, cCustomer c) { return distance(l->id, c->at) * (*vX)(l, c); }).build();
    model.add(IloMinimize(env, objective));
    objective.end();
  });
  r.measure("assign", [&] {
    const auto assignedTo = sASSIGN.groupBy<cCustomer>();
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

#  else

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

void runCalls(const Instance& /*in*/, Result& r) {
  r.skip("a call a row is HiGHS's own API");
}

#  endif

#endif

#if defined(SCENARIO_CALLABLE) && !SCENARIO_DREAMOR

// CPLEX's Callable Library, as its own examples write a model: columns y[l] first, then
// x[l * C + c]; the arrays filled by position, the rows as CPXaddrows takes them (a begin, then
// indices and values, a sense and a right-hand side each), handed over in one CPXnewcols and one
// CPXaddrows.
void runCallable(const Instance& in, Result& r) {
  const std::size_t l_count = in.locations.size(), c_count = in.customers.size();
  const std::size_t n = l_count + l_count * c_count;
  const auto x        = [&](std::size_t l, std::size_t c) { return static_cast<int>(l_count + l * c_count + c); };
  const Callable cpx;
  std::vector<double> obj, lower, upper, rhs, value;
  std::vector<char> type, sense;
  std::vector<int> begin, index;
  const auto endRow = [&](char row_sense, double row_rhs) {
    sense.push_back(row_sense);
    rhs.push_back(row_rhs);
  };
  r.measure("sets", [] {});
  r.measure("vars", [&] {
    lower.assign(n, 0);
    upper.assign(n, 1);
    type.assign(n, 'C');
    std::fill_n(type.begin(), l_count, 'B');
  });
  r.measure("objective", [&] {
    obj.assign(n, 0);
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        obj[static_cast<std::size_t>(x(l, c))] = distance(static_cast<std::uint32_t>(l), in.customers[c].at);
      }
    }
  });
  r.measure("assign", [&] {
    for (std::size_t c = 0; c < c_count; ++c) {
      begin.push_back(static_cast<int>(index.size()));
      for (std::size_t l = 0; l < l_count; ++l) {
        index.push_back(x(l, c));
        value.push_back(1);
      }
      endRow('E', 1);
    }
  });
  r.measure("link", [&] {
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        begin.push_back(static_cast<int>(index.size()));
        index.insert(index.end(), {static_cast<int>(l), x(l, c)});
        value.insert(value.end(), {-1, 1});
        endRow('L', 0);
      }
    }
  });
  r.measure("budget", [&] {
    begin.push_back(static_cast<int>(index.size()));
    for (std::size_t l = 0; l < l_count; ++l) {
      index.push_back(static_cast<int>(l));
      value.push_back(1);
    }
    endRow('E', static_cast<double>(in.p));
  });
  r.measure("extract", [&] {
    r.sink +=
        CPXnewcols(
            cpx.env(), cpx.lp(), static_cast<int>(n), obj.data(), lower.data(), upper.data(), type.data(), nullptr) ==
        0;
    r.sink += CPXaddrows(cpx.env(),
                         cpx.lp(),
                         0,
                         static_cast<int>(rhs.size()),
                         static_cast<int>(index.size()),
                         rhs.data(),
                         sense.data(),
                         begin.data(),
                         index.data(),
                         value.data(),
                         nullptr,
                         nullptr) == 0;
  });
  start(cpx, r);
}

// The Callable Library a call a row: the columns in two CPXnewcols, the costs in one CPXchgobj,
// every row handed to CPLEX as it is made (CPXaddrows of one row).
void runCallableRows(const Instance& in, Result& r) {
  const std::size_t l_count = in.locations.size(), c_count = in.customers.size();
  const auto x = [&](std::size_t l, std::size_t c) { return static_cast<int>(l_count + l * c_count + c); };
  const Callable cpx;
  const auto addRow = [&](char row_sense, double row_rhs, std::size_t count, const int* index, const double* value) {
    const int begin = 0;
    r.sink += CPXaddrows(cpx.env(),
                         cpx.lp(),
                         0,
                         1,
                         static_cast<int>(count),
                         &row_rhs,
                         &row_sense,
                         &begin,
                         index,
                         value,
                         nullptr,
                         nullptr) == 0;
  };
  r.measure("sets", [] {});
  r.measure("vars", [&] {
    const std::vector<double> lower(l_count * c_count, 0), upper(l_count * c_count, 1);
    const std::vector<char> binary(l_count, 'B'), continuous(l_count * c_count, 'C');
    CPXnewcols(
        cpx.env(), cpx.lp(), static_cast<int>(l_count), nullptr, lower.data(), upper.data(), binary.data(), nullptr);
    CPXnewcols(cpx.env(),
               cpx.lp(),
               static_cast<int>(l_count * c_count),
               nullptr,
               lower.data(),
               upper.data(),
               continuous.data(),
               nullptr);
  });
  r.measure("objective", [&] {
    std::vector<int> index(l_count * c_count);
    std::vector<double> cost(l_count * c_count);
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        index[l * c_count + c] = x(l, c);
        cost[l * c_count + c]  = distance(static_cast<std::uint32_t>(l), in.customers[c].at);
      }
    }
    CPXchgobj(cpx.env(), cpx.lp(), static_cast<int>(index.size()), index.data(), cost.data());
  });
  r.measure("assign", [&] {
    std::vector<int> index(l_count);
    const std::vector<double> ones(l_count, 1);
    for (std::size_t c = 0; c < c_count; ++c) {
      for (std::size_t l = 0; l < l_count; ++l) {
        index[l] = x(l, c);
      }
      addRow('E', 1, l_count, index.data(), ones.data());
    }
  });
  r.measure("link", [&] {
    const std::array<double, 2> value{-1, 1};
    for (std::size_t l = 0; l < l_count; ++l) {
      for (std::size_t c = 0; c < c_count; ++c) {
        const std::array<int, 2> index{static_cast<int>(l), x(l, c)};
        addRow('L', 0, 2, index.data(), value.data());
      }
    }
  });
  r.measure("budget", [&] {
    std::vector<int> index(l_count);
    for (std::size_t l = 0; l < l_count; ++l) {
      index[l] = static_cast<int>(l);
    }
    const std::vector<double> ones(l_count, 1);
    addRow('E', static_cast<double>(in.p), l_count, index.data(), ones.data());
  });
  r.measure("extract", [] {});
  start(cpx, r);
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
  if (impl != "dreamor" && impl != "hand" && impl != "calls" && impl != "callable" && impl != "callable-rows") {
    usage("--impl dreamor|hand|calls|callable|callable-rows");
  }
  if (l == 0 || c == 0 || p > l) { usage("--L, --C must be positive and --P at most --L"); }

  Result r;
  Instance in;
  r.setup("heap", [&] { preconditionHeap(heap_seed); });
  r.setup("instance", [&] { in = makeInstance(l, c, p, seed); });
  if (!pmu_spec.empty()) { pmu().open(pmu_spec); }
#if SCENARIO_DREAMOR
  if (impl == "dreamor") {
    runDreamor(in, r);
  } else {
    r.skip("no arm " + impl + " in ./pmedian_" + kBackend + ": the hand-written arms are ./pmedian_" + kBackend +
           "_hand");
  }
#else
  if (impl == "hand") {
    runHand(in, r);
  } else if (impl == "calls") {
    runCalls(in, r);
  } else if (impl == "dreamor") {
    r.skip(std::string{"no arm dreamor in ./pmedian_"} + kBackend + "_hand: it is ./pmedian_" + kBackend);
  } else {
#  if defined(SCENARIO_CALLABLE)
    if (impl == "callable") {
      runCallable(in, r);
    } else {
      runCallableRows(in, r);
    }
#  else
    r.skip("the Callable Library is CPLEX's");
#  endif
  }
#endif
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
