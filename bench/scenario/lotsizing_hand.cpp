// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// scenario/lotsizing_hand.cpp - lot sizing's hand-written arms, which lotsizing.hpp describes: plain, flat, callable
// and callable-rows.
//
// build.sh builds them as ./lotsizing_<backend>_hand without dreamor's include path, so that no dreamor header reaches
// them.
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../common/result.hpp"
#include "backend.hpp"
#include "lotsizing.hpp"
#if !defined(SCENARIO_MATHOPT) && !defined(SCENARIO_HIGHS)
#  include "../../examples/production_planning_plain.hpp"
#endif

namespace {

// What a programmer who writes the model by hand keeps beside the data: the routes in machine order, and the routes
// of each item. machine, item and item_number are indexed by route. item_number is the item's place in
// Instance::items. Machine m's routes are [machine_start[m], machine_start[m + 1]). Item i's routes are
// of_item[item_start[i]] up to of_item[item_start[i + 1]], that last one excluded.
struct Routes {
  std::vector<const planning::MachineRecord*> machine;
  std::vector<const planning::ItemRecord*> item;
  std::vector<std::uint32_t> item_number;
  std::vector<std::uint32_t> machine_start;
  std::vector<std::uint32_t> item_start, of_item;

  explicit Routes(const Instance& in) {
    // The data lists the routes in no particular order, so a stable counting sort puts them in machine order.
    machine_start.assign(in.machines.size() + 1, 0);
    for (const planning::RouteRecord& route : in.routes) {
      ++machine_start[route.machine + 1];
    }
    for (std::size_t m = 0; m < in.machines.size(); ++m) {
      machine_start[m + 1] += machine_start[m];
    }
    std::vector<std::uint32_t> in_order(in.routes.size());
    std::vector<std::uint32_t> next_of_machine(machine_start.begin(), machine_start.end() - 1);
    for (std::uint32_t r = 0; r < in.routes.size(); ++r) {
      in_order[next_of_machine[in.routes[r].machine]++] = r;
    }
    for (const std::uint32_t r : in_order) {
      const planning::RouteRecord& route = in.routes[r];
      machine.push_back(&in.machines[route.machine]);
      item.push_back(&in.items[route.item]);
      item_number.push_back(static_cast<std::uint32_t>(route.item));
    }
    item_start.assign(in.items.size() + 1, 0);
    for (const std::uint32_t i : item_number) {
      ++item_start[i + 1];
    }
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      item_start[i + 1] += item_start[i];
    }
    of_item.resize(item.size());
    std::vector<std::uint32_t> next(item_start.begin(), item_start.end() - 1);
    for (std::uint32_t r = 0; r < item.size(); ++r) {
      of_item[next[item_number[r]]++] = r;
    }
  }
};

// Writes a variable's name, Family_<name>_<name>_<period> or Family_<name>_<period>, as dreamor and the example
// name the variables.
class Name {
 public:
  explicit Name(std::string_view family) : at_(family.size()) { std::memcpy(buf_.data(), family.data(), at_); }

  const char* operator()(std::string_view a, std::string_view b, std::size_t period) {
    char* p = buf_.data() + at_;
    p       = put(put(p, a), b);
    *p++    = '_';
    p       = std::to_chars(p, buf_.data() + buf_.size() - 1, period).ptr;
    *p      = '\0';
    return buf_.data();
  }

  const char* operator()(std::string_view a, std::size_t period) {
    char* p = put(buf_.data() + at_, a);
    *p++    = '_';
    p       = std::to_chars(p, buf_.data() + buf_.size() - 1, period).ptr;
    *p      = '\0';
    return buf_.data();
  }

 private:
  static char* put(char* p, std::string_view s) {
    *p++ = '_';
    std::memcpy(p, s.data(), s.size());
    return p + s.size();
  }

  std::array<char, 256> buf_{};
  std::size_t at_;
};

#if defined(SCENARIO_MATHOPT)

void runFlat(const Instance& in, Result& r) {
  const std::size_t t_count = in.periods.size();
  math_opt::Model model;
  std::optional<Routes> routes;
  // yield and setup hold a variable at route * T + t, and stock at item * T + t.
  std::vector<math_opt::Variable> yield, setup, stock;
  r.measure("variables", [&] {
    routes.emplace(in);
    const std::size_t r_count = routes->item.size();
    yield.reserve(r_count * t_count);
    setup.reserve(r_count * t_count);
    stock.reserve(in.items.size() * t_count);
    Name yield_name{"Yield"}, setup_name{"Setup"}, stock_name{"Stock"};
    for (std::size_t q = 0; q < r_count; ++q) {
      const planning::MachineRecord* m = routes->machine[q];
      const planning::ItemRecord* i    = routes->item[q];
      for (const std::size_t t : in.periods) {
        yield.push_back(model.AddVariable(0, m->capacity / i->time, false, yield_name(m->name, i->name, t)));
        setup.push_back(model.AddVariable(0, 1, true, setup_name(m->name, i->name, t)));
      }
    }
    for (const auto& i : in.items) {
      for (const std::size_t t : in.periods) {
        stock.push_back(model.AddVariable(0, std::numeric_limits<double>::infinity(), false, stock_name(i.name, t)));
      }
    }
  });
  r.measure("constraints", [&] {
    using math_opt::LinearExpression;
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (std::size_t t = 0; t < t_count; ++t) {
        LinearExpression in_flow;
        for (std::uint32_t k = routes->item_start[i]; k < routes->item_start[i + 1]; ++k) {
          in_flow += yield[routes->of_item[k] * t_count + t];
        }
        if (t > 0) { in_flow += stock[i * t_count + t - 1]; }
        model.AddLinearConstraint(std::move(in_flow) - in.items[i].demand[t] == stock[i * t_count + t]);
      }
    }
    for (std::size_t m = 0; m < in.machines.size(); ++m) {
      const std::uint32_t first = routes->machine_start[m], last = routes->machine_start[m + 1];
      if (first == last) { continue; }
      for (std::size_t t = 0; t < t_count; ++t) {
        LinearExpression load;
        for (std::uint32_t q = first; q < last; ++q) {
          load += routes->item[q]->time * yield[q * t_count + t];
        }
        model.AddLinearConstraint(std::move(load) <= in.machines[m].capacity);
      }
    }
    for (std::size_t v = 0; v < yield.size(); ++v) {
      model.AddLinearConstraint(yield[v] <= yield[v].upper_bound() * setup[v]);
    }
    LinearExpression cost;
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (std::size_t t = 0; t < t_count; ++t) {
        cost += in.items[i].hold * stock[i * t_count + t];
      }
    }
    for (std::size_t q = 0; q < routes->item.size(); ++q) {
      for (std::size_t t = 0; t < t_count; ++t) {
        cost += routes->machine[q]->setup * setup[q * t_count + t];
      }
    }
    model.Minimize(std::move(cost));
  });
  solve(model, r);
}

void runPlain(const Instance& /*in*/, Result& r) {
  r.skip("the example's plain model is Concert only");
}

#elif defined(SCENARIO_HIGHS)

// The columns are Yield at route * T + t, then Setup in the same order, then Stock at item * T + t. The rows are
// stored row-wise.
void runFlat(const Instance& in, Result& r) {
  const std::size_t t_count = in.periods.size();
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
  std::optional<Routes> routes;
  // The first column of each family.
  std::size_t yield = 0, setup = 0, stock = 0;
  r.measure("variables", [&] {
    routes.emplace(in);
    const std::size_t r_count = routes->item.size();
    const std::size_t n       = 2 * r_count * t_count + in.items.size() * t_count;
    setup                     = r_count * t_count;
    stock                     = 2 * r_count * t_count;
    lp.col_lower_.assign(n, 0);
    lp.col_upper_.resize(n);
    lp.col_cost_.assign(n, 0);
    lp.integrality_.assign(n, HighsVarType::kContinuous);
    lp.col_names_.resize(n);
    Name yield_name{"Yield"}, setup_name{"Setup"}, stock_name{"Stock"};
    for (std::size_t q = 0; q < r_count; ++q) {
      const planning::MachineRecord* m = routes->machine[q];
      const planning::ItemRecord* i    = routes->item[q];
      for (const std::size_t t : in.periods) {
        const std::size_t v        = q * t_count + t;
        lp.col_upper_[yield + v]   = m->capacity / i->time;
        lp.col_names_[yield + v]   = yield_name(m->name, i->name, t);
        lp.col_upper_[setup + v]   = 1;
        lp.integrality_[setup + v] = HighsVarType::kInteger;
        lp.col_names_[setup + v]   = setup_name(m->name, i->name, t);
      }
    }
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (const std::size_t t : in.periods) {
        lp.col_upper_[stock + i * t_count + t] = kHighsInf;
        lp.col_names_[stock + i * t_count + t] = stock_name(in.items[i].name, t);
      }
    }
  });
  r.measure("constraints", [&] {
    const auto put = [&](std::size_t column, double value) {
      a.index_.push_back(static_cast<HighsInt>(column));
      a.value_.push_back(value);
    };
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (std::size_t t = 0; t < t_count; ++t) {
        for (std::uint32_t k = routes->item_start[i]; k < routes->item_start[i + 1]; ++k) {
          put(yield + routes->of_item[k] * t_count + t, 1);
        }
        if (t > 0) { put(stock + i * t_count + t - 1, 1); }
        put(stock + i * t_count + t, -1);
        endRow(in.items[i].demand[t], in.items[i].demand[t]);
      }
    }
    for (std::size_t m = 0; m < in.machines.size(); ++m) {
      const std::uint32_t first = routes->machine_start[m], last = routes->machine_start[m + 1];
      if (first == last) { continue; }
      for (std::size_t t = 0; t < t_count; ++t) {
        for (std::uint32_t q = first; q < last; ++q) {
          put(yield + q * t_count + t, routes->item[q]->time);
        }
        endRow(-kHighsInf, in.machines[m].capacity);
      }
    }
    for (std::size_t v = 0; v < setup; ++v) {
      put(yield + v, 1);
      put(setup + v, -lp.col_upper_[yield + v]);
      endRow(-kHighsInf, 0);
    }
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (std::size_t t = 0; t < t_count; ++t) {
        lp.col_cost_[stock + i * t_count + t] = in.items[i].hold;
      }
    }
    for (std::size_t q = 0; q < routes->item.size(); ++q) {
      for (std::size_t t = 0; t < t_count; ++t) {
        lp.col_cost_[setup + q * t_count + t] = routes->machine[q]->setup;
      }
    }
  });
  lp.num_col_ = a.num_col_ = static_cast<HighsInt>(lp.col_cost_.size());
  lp.num_row_ = a.num_row_ = static_cast<HighsInt>(lp.row_lower_.size());
  solve(highs, std::move(model), r);
}

void runPlain(const Instance& /*in*/, Result& r) {
  r.skip("the example's plain model is Concert only");
}

#else  // The backend is Concert: real CPLEX or the stub.

void runPlain(const Instance& in, Result& r) {
  IloEnv env;
  IloModel model(env);
  std::optional<planning::PlainModel> m;
  r.measure("variables", [&] { m.emplace(env, in); });
  r.measure("constraints", [&] { m->build(env, model, in); });
  solve(env, model, r);
  env.end();
}

void runFlat(const Instance& in, Result& r) {
  const std::size_t t_count = in.periods.size();
  IloEnv env;
  IloModel model(env);
  std::optional<Routes> routes;
  // yield and setup hold a variable at route * T + t, and stock at item * T + t.
  std::vector<IloNumVar> yield, setup, stock;
  r.measure("variables", [&] {
    routes.emplace(in);
    const std::size_t r_count = routes->item.size();
    yield.reserve(r_count * t_count);
    setup.reserve(r_count * t_count);
    stock.reserve(in.items.size() * t_count);
    Name yield_name{"Yield"}, setup_name{"Setup"}, stock_name{"Stock"};
    for (std::size_t q = 0; q < r_count; ++q) {
      const planning::MachineRecord* m = routes->machine[q];
      const planning::ItemRecord* i    = routes->item[q];
      for (const std::size_t t : in.periods) {
        yield.emplace_back(env, 0, m->capacity / i->time, ILOFLOAT, yield_name(m->name, i->name, t));
        setup.emplace_back(env, 0, 1, ILOBOOL, setup_name(m->name, i->name, t));
      }
    }
    for (const auto& i : in.items) {
      for (const std::size_t t : in.periods) {
        stock.emplace_back(env, 0, IloInfinity, ILOFLOAT, stock_name(i.name, t));
      }
    }
  });
  r.measure("constraints", [&] {
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (std::size_t t = 0; t < t_count; ++t) {
        IloExpr in_flow(env);
        for (std::uint32_t k = routes->item_start[i]; k < routes->item_start[i + 1]; ++k) {
          in_flow += yield[routes->of_item[k] * t_count + t];
        }
        if (t > 0) { in_flow += stock[i * t_count + t - 1]; }
        model.add(in_flow - in.items[i].demand[t] == stock[i * t_count + t]);
        in_flow.end();
      }
    }
    for (std::size_t m = 0; m < in.machines.size(); ++m) {
      const std::uint32_t first = routes->machine_start[m], last = routes->machine_start[m + 1];
      if (first == last) { continue; }
      for (std::size_t t = 0; t < t_count; ++t) {
        IloExpr load(env);
        for (std::uint32_t q = first; q < last; ++q) {
          load += routes->item[q]->time * yield[q * t_count + t];
        }
        model.add(load <= in.machines[m].capacity);
        load.end();
      }
    }
    for (std::size_t v = 0; v < yield.size(); ++v) {
      model.add(yield[v] <= yield[v].getUB() * setup[v]);
    }
    IloExpr cost(env);
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (std::size_t t = 0; t < t_count; ++t) {
        cost += in.items[i].hold * stock[i * t_count + t];
      }
    }
    for (std::size_t q = 0; q < routes->item.size(); ++q) {
      for (std::size_t t = 0; t < t_count; ++t) {
        cost += routes->machine[q]->setup * setup[q * t_count + t];
      }
    }
    model.add(IloMinimize(env, cost));
    cost.end();
  });
  solve(env, model, r);
  env.end();
}

#endif

#if defined(SCENARIO_CALLABLE)

// The model's columns, in the arrays that CPXnewcols takes: Yield at route * T + t, then Setup in the same order, then
// Stock at item * T + t. The arrays hold the columns' bounds, types, costs and names. The names are copied into one
// buffer of null-terminated strings, as C code keeps them.
struct CallableColumns {
  std::vector<double> lower, upper, cost;
  std::vector<char> type, names;
  std::vector<std::size_t> name_at;
  // The first column of each family.
  std::size_t yield = 0, setup = 0, stock = 0;

  CallableColumns(const Instance& in, const Routes& routes) {
    const std::size_t t_count = in.periods.size(), r_count = routes.item.size();
    const std::size_t n = 2 * r_count * t_count + in.items.size() * t_count;
    setup               = r_count * t_count;
    stock               = 2 * r_count * t_count;
    lower.assign(n, 0);
    upper.resize(n);
    cost.assign(n, 0);
    type.assign(n, 'C');
    name_at.resize(n);
    Name yield_name{"Yield"}, setup_name{"Setup"}, stock_name{"Stock"};
    const auto name = [&](std::size_t column, const char* text) {
      name_at[column] = names.size();
      names.insert(names.end(), text, text + std::strlen(text) + 1);
    };
    for (std::size_t q = 0; q < r_count; ++q) {
      const planning::MachineRecord* m = routes.machine[q];
      const planning::ItemRecord* i    = routes.item[q];
      for (const std::size_t t : in.periods) {
        const std::size_t v = q * t_count + t;
        upper[yield + v]    = m->capacity / i->time;
        name(yield + v, yield_name(m->name, i->name, t));
        upper[setup + v] = 1;
        type[setup + v]  = 'B';
        name(setup + v, setup_name(m->name, i->name, t));
      }
    }
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (const std::size_t t : in.periods) {
        upper[stock + i * t_count + t] = CPX_INFBOUND;
        name(stock + i * t_count + t, stock_name(in.items[i].name, t));
      }
    }
  }

  // Hands every column over in one call. CPXnewcols takes the names as char**, but it only reads them.
  [[nodiscard]] bool add(const Callable& cpx) {
    std::vector<char*> name_of(name_at.size());
    for (std::size_t k = 0; k < name_at.size(); ++k) {
      name_of[k] = names.data() + name_at[k];
    }
    return CPXnewcols(cpx.env(),
                      cpx.lp(),
                      static_cast<int>(lower.size()),
                      cost.data(),
                      lower.data(),
                      upper.data(),
                      type.data(),
                      name_of.data()) == 0;
  }
};

// Writes every row of the model, in the order in which the other arms add the rows. Each coefficient goes to
// put(column, coefficient), and the end of each row goes to emit(sense, rhs). The costs go into cost.
template <typename Put, typename Emit>
void eachRow(const Instance& in,
             const Routes& routes,
             const CallableColumns& columns,
             std::vector<double>& cost,
             const Put& put,
             const Emit& emit) {
  const std::size_t t_count = in.periods.size();
  for (std::size_t i = 0; i < in.items.size(); ++i) {
    for (std::size_t t = 0; t < t_count; ++t) {
      for (std::uint32_t k = routes.item_start[i]; k < routes.item_start[i + 1]; ++k) {
        put(columns.yield + routes.of_item[k] * t_count + t, 1);
      }
      if (t > 0) { put(columns.stock + i * t_count + t - 1, 1); }
      put(columns.stock + i * t_count + t, -1);
      emit('E', in.items[i].demand[t]);
    }
  }
  for (std::size_t m = 0; m < in.machines.size(); ++m) {
    const std::uint32_t first = routes.machine_start[m], last = routes.machine_start[m + 1];
    if (first == last) { continue; }
    for (std::size_t t = 0; t < t_count; ++t) {
      for (std::uint32_t q = first; q < last; ++q) {
        put(columns.yield + q * t_count + t, routes.item[q]->time);
      }
      emit('L', in.machines[m].capacity);
    }
  }
  for (std::size_t v = 0; v < columns.setup; ++v) {
    put(columns.yield + v, 1);
    put(columns.setup + v, -columns.upper[columns.yield + v]);
    emit('L', 0);
  }
  for (std::size_t i = 0; i < in.items.size(); ++i) {
    for (std::size_t t = 0; t < t_count; ++t) {
      cost[columns.stock + i * t_count + t] = in.items[i].hold;
    }
  }
  for (std::size_t q = 0; q < routes.item.size(); ++q) {
    for (std::size_t t = 0; t < t_count; ++t) {
      cost[columns.setup + q * t_count + t] = routes.machine[q]->setup;
    }
  }
}

// The Callable Library, as CPLEX's own examples write a model. Every array is filled by position, and one CPXnewcols
// and one CPXaddrows hand the arrays over.
void runCallable(const Instance& in, Result& r) {
  const Callable cpx;
  std::optional<Routes> routes;
  std::optional<CallableColumns> columns;
  std::vector<double> rhs, value;
  std::vector<char> sense;
  std::vector<int> begin, index;
  r.measure("variables", [&] {
    routes.emplace(in);
    columns.emplace(in, *routes);
  });
  r.measure("constraints", [&] {
    const auto put = [&](std::size_t column, double coefficient) {
      index.push_back(static_cast<int>(column));
      value.push_back(coefficient);
    };
    std::size_t row_start = 0;
    const auto emit       = [&](char row_sense, double row_rhs) {
      begin.push_back(static_cast<int>(row_start));
      row_start = index.size();
      sense.push_back(row_sense);
      rhs.push_back(row_rhs);
    };
    eachRow(in, *routes, *columns, columns->cost, put, emit);
  });
  r.measure("extract", [&] {
    r.sink += columns->add(cpx);
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

// The Callable Library with one call for each row. The named columns go in one CPXnewcols. Every row is handed to
// CPLEX as it is made, and the costs go in one CPXchgobj.
void runCallableRows(const Instance& in, Result& r) {
  const Callable cpx;
  std::optional<Routes> routes;
  std::optional<CallableColumns> columns;
  r.measure("variables", [&] {
    routes.emplace(in);
    columns.emplace(in, *routes);
    r.sink += columns->add(cpx);
  });
  r.measure("constraints", [&] {
    // The indices and values of the row that is being made.
    std::vector<int> index;
    std::vector<double> value;
    const auto put = [&](std::size_t column, double coefficient) {
      index.push_back(static_cast<int>(column));
      value.push_back(coefficient);
    };
    const auto emit = [&](char row_sense, double row_rhs) {
      const int begin = 0;
      r.sink += CPXaddrows(cpx.env(),
                           cpx.lp(),
                           0,
                           1,
                           static_cast<int>(index.size()),
                           &row_rhs,
                           &row_sense,
                           &begin,
                           index.data(),
                           value.data(),
                           nullptr,
                           nullptr) == 0;
      index.clear();
      value.clear();
    };
    std::vector<double> cost(columns->cost.size(), 0);
    eachRow(in, *routes, *columns, cost, put, emit);
    std::vector<int> all(cost.size());
    for (std::size_t k = 0; k < all.size(); ++k) {
      all[k] = static_cast<int>(k);
    }
    CPXchgobj(cpx.env(), cpx.lp(), static_cast<int>(all.size()), all.data(), cost.data());
  });
  r.measure("extract", [] {});
  start(cpx, r);
}
#endif

// Runs one of the binary's hand-written arms. The dreamor arm is in ./lotsizing_<backend>.
void runArm(const std::string& impl, const Instance& in, Result& r) {
  if (impl == "plain") {
    runPlain(in, r);
  } else if (impl == "flat") {
    runFlat(in, r);
  } else if (impl == "dreamor") {
    r.skip(std::string{"no arm dreamor in ./lotsizing_"} + kBackend + "_hand: it is ./lotsizing_" + kBackend);
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
  scenarioMain(argc, argv, runArm);
}
