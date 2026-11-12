// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// scenario/pmedian_hand.cpp - p-median's hand-written arms, which pmedian.hpp describes: hand, calls, callable and
// callable-rows.
//
// build.sh builds them as ./pmedian_<backend>_hand without dreamor's include path, so that no dreamor header reaches
// them.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../common/result.hpp"
#include "backend.hpp"
#include "pmedian.hpp"

namespace {

// The hand-written arms hold the entities in std::vectors.
template <typename T>
using Entities = std::vector<T>;

template <typename T>
void add(Entities<T>& entities, const T& entity) {
  entities.push_back(entity);
}

template <typename T>
void seal(Entities<T>& /*entities*/) {}

using Instance = BasicInstance<Entities>;

#if defined(SCENARIO_MATHOPT)

void runHand(const Instance& in, Result& r) {
  const std::size_t l_count = in.locations.size(), c_count = in.customers.size();
  math_opt::Model model;
  // x holds x[l, c] at l * C + c.
  std::vector<math_opt::Variable> y, x;
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

#elif defined(SCENARIO_HIGHS)

// The columns are y[l] first, then x[l, c] at l * C + c. The rows are stored row-wise, as HighsEnv collects them.
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

// HiGHS's own API with one call for each row, as HiGHS's examples write a model. The columns go in two calls, and
// every row is handed to the Highs as it is made.
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

#else  // The backend is Concert: real CPLEX or the stub.

void runHand(const Instance& in, Result& r) {
  const std::size_t l_count = in.locations.size(), c_count = in.customers.size();
  IloEnv env;
  IloModel model(env);
  // x holds x[l, c] at l * C + c.
  std::vector<IloNumVar> y, x;
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

#endif

#if defined(SCENARIO_CALLABLE)

// CPLEX's Callable Library, as CPLEX's own examples write a model. The columns are y[l] first, then x[l, c] at
// l * C + c. The arrays are filled by position. The rows are in the arrays that CPXaddrows takes: for each row, a
// begin, the indices and values, a sense and a right-hand side. One CPXnewcols and one CPXaddrows hand them over.
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

// The Callable Library with one call for each row. The columns go in two CPXnewcols, and the costs in one CPXchgobj.
// Every row is handed to CPLEX as it is made, by a CPXaddrows of one row.
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

// Runs one of the binary's hand-written arms. The dreamor arm is in ./pmedian_<backend>.
void runArm(const std::string& impl, const Instance& in, Result& r) {
  if (impl == "hand") {
    runHand(in, r);
  } else if (impl == "calls") {
    runCalls(in, r);
  } else if (impl == "dreamor") {
    r.skip(std::string{"no arm dreamor in ./pmedian_"} + kBackend + "_hand: it is ./pmedian_" + kBackend);
  } else {
#if defined(SCENARIO_CALLABLE)
    if (impl == "callable") {
      runCallable(in, r);
    } else {
      runCallableRows(in, r);
    }
#else
    r.skip("the Callable Library is CPLEX's");
#endif
  }
}

}  // namespace

int main(int argc, char** argv) {
  scenarioMain<Entities>(argc, argv, runArm);
}
