// SPDX-License-Identifier: Apache-2.0
// scenario/lotsizing.cpp - the repository's lot-sizing example, built up to the moment a solver
// starts on it: one arm, one size, one repetition, in one process; prints one JSON line.
//
// The model and its data are examples/production_planning*.hpp's, as they are: M machines, each
// routing about 60% of I items, over T periods. Yield and Setup on every (machine, item, period)
// the routes have, Stock on every (item, period); a stock balance an (item, period), a capacity a
// (machine, period), a setup link a production row, and the costs of holding and of setups. Every
// variable is named, as the example names them. Phases, the same in each arm:
//   variables    the sets, or whatever an arm keeps instead, and the three families
//   constraints  every constraint and the objective, handed to the model
//   extract      the model handed to the solver
//   solve0       a solve with a time limit of zero
// Against the stub there is no solver, so a run stops after constraints.
//
// Arms:
//   dreamor  planning::Model / planning::MathOptModel, the example itself
//   plain    planning::PlainModel, the example's own comparison: std::maps by the entities and
//            loops over the data, the way the model is usually written without index sets
//            (Concert only, as in the example)
//   flat     hand-optimized: routes in machine order, each item's routes as CSR, variables in
//            std::vectors by position, names written with std::to_chars
//
//   lotsizing --impl dreamor|plain|flat --items <I> [--machines 20] [--periods 50] [--rep r]
//             [--seed s] [--heap-seed h] [--pmu <events>]
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "backend.hpp"
#include "dreamor/logger/logger_light.hpp"
#if defined(SCENARIO_MATHOPT)
#  include "../../examples/production_planning_mathopt.hpp"
#else
#  include "../../examples/production_planning_plain.hpp"
#endif

namespace {

using planning::Instance;

// What a hand-coder keeps beside the data: the routes in machine order, the routes of each item.
struct Routes {
  std::vector<const planning::Machine*> machine;   // by route
  std::vector<const planning::Item*> item;         // by route
  std::vector<std::uint32_t> item_number;          // by route: the item's place in Instance::items
  std::vector<std::uint32_t> machine_start;        // machine m's routes: [machine_start[m], machine_start[m + 1])
  std::vector<std::uint32_t> item_start, of_item;  // item i's routes: of_item[item_start[i] .. item_start[i + 1])

  explicit Routes(const Instance& in) {
    std::unordered_map<const planning::Item*, std::uint32_t> number;
    number.reserve(in.items.size());
    for (std::uint32_t i = 0; i < in.items.size(); ++i) number.emplace(in.items[i].get(), i);
    machine_start.push_back(0);
    for (const auto& m : in.machines) {
      for (const planning::Item* it : m->items) {
        machine.push_back(m.get());
        item.push_back(it);
        item_number.push_back(number.at(it));
      }
      machine_start.push_back(static_cast<std::uint32_t>(item.size()));
    }
    item_start.assign(in.items.size() + 1, 0);
    for (const std::uint32_t i : item_number) ++item_start[i + 1];
    for (std::size_t i = 0; i < in.items.size(); ++i) item_start[i + 1] += item_start[i];
    of_item.resize(item.size());
    std::vector<std::uint32_t> next(item_start.begin(), item_start.end() - 1);
    for (std::uint32_t r = 0; r < item.size(); ++r) of_item[next[item_number[r]]++] = r;
  }
};

// Family_<name>_<name>_<period>, as dreamor and the example name them.
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

void runDreamor(const Instance& in, Result& r) {
  math_opt::Model model;
  std::optional<planning::MathOptModel> m;
  r.measure("variables", [&] { m.emplace(model, in); });
  r.measure("constraints", [&] { m->build(model); });
  solve(model, r);
}

void runFlat(const Instance& in, Result& r) {
  const std::size_t t_count = in.periods.size();
  math_opt::Model model;
  std::optional<Routes> routes;
  std::vector<math_opt::Variable> yield, setup, stock;  // [route * T + t], [route * T + t], [item * T + t]
  r.measure("variables", [&] {
    routes.emplace(in);
    const std::size_t r_count = routes->item.size();
    yield.reserve(r_count * t_count);
    setup.reserve(r_count * t_count);
    stock.reserve(in.items.size() * t_count);
    Name yield_name{"Yield"}, setup_name{"Setup"}, stock_name{"Stock"};
    for (std::size_t q = 0; q < r_count; ++q) {
      const planning::Machine* m = routes->machine[q];
      const planning::Item* i    = routes->item[q];
      for (const std::size_t t : in.periods) {
        yield.push_back(model.AddVariable(0, m->capacity / i->time, false, yield_name(m->name, i->name, t)));
        setup.push_back(model.AddVariable(0, 1, true, setup_name(m->name, i->name, t)));
      }
    }
    for (const auto& i : in.items)
      for (const std::size_t t : in.periods)
        stock.push_back(model.AddVariable(0, std::numeric_limits<double>::infinity(), false, stock_name(i->name, t)));
  });
  r.measure("constraints", [&] {
    using math_opt::LinearExpression;
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (std::size_t t = 0; t < t_count; ++t) {
        LinearExpression in_flow;
        for (std::uint32_t k = routes->item_start[i]; k < routes->item_start[i + 1]; ++k)
          in_flow += yield[routes->of_item[k] * t_count + t];
        if (t > 0) in_flow += stock[i * t_count + t - 1];
        model.AddLinearConstraint(std::move(in_flow) - in.items[i]->demand[t] == stock[i * t_count + t]);
      }
    }
    for (std::size_t m = 0; m < in.machines.size(); ++m) {
      const std::uint32_t first = routes->machine_start[m], last = routes->machine_start[m + 1];
      if (first == last) continue;
      for (std::size_t t = 0; t < t_count; ++t) {
        LinearExpression load;
        for (std::uint32_t q = first; q < last; ++q) load += routes->item[q]->time * yield[q * t_count + t];
        model.AddLinearConstraint(std::move(load) <= in.machines[m]->capacity);
      }
    }
    for (std::size_t v = 0; v < yield.size(); ++v)
      model.AddLinearConstraint(yield[v] <= yield[v].upper_bound() * setup[v]);
    LinearExpression cost;
    for (std::size_t i = 0; i < in.items.size(); ++i)
      for (std::size_t t = 0; t < t_count; ++t) cost += in.items[i]->hold * stock[i * t_count + t];
    for (std::size_t q = 0; q < routes->item.size(); ++q)
      for (std::size_t t = 0; t < t_count; ++t) cost += routes->machine[q]->setup * setup[q * t_count + t];
    model.Minimize(std::move(cost));
  });
  solve(model, r);
}

void runPlain(const Instance& /*in*/, Result& r) {
  r.skip("the example's plain model is Concert only");
}

#else  // Concert, real or the stub

void runDreamor(const Instance& in, Result& r) {
  IloEnv env;
  IloModel model(env);
  std::optional<planning::Model> m;
  r.measure("variables", [&] { m.emplace(env, in); });
  r.measure("constraints", [&] { m->build(env, model); });
  solve(env, model, r);
  env.end();
}

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
  std::vector<IloNumVar> yield, setup, stock;  // [route * T + t], [route * T + t], [item * T + t]
  r.measure("variables", [&] {
    routes.emplace(in);
    const std::size_t r_count = routes->item.size();
    yield.reserve(r_count * t_count);
    setup.reserve(r_count * t_count);
    stock.reserve(in.items.size() * t_count);
    Name yield_name{"Yield"}, setup_name{"Setup"}, stock_name{"Stock"};
    for (std::size_t q = 0; q < r_count; ++q) {
      const planning::Machine* m = routes->machine[q];
      const planning::Item* i    = routes->item[q];
      for (const std::size_t t : in.periods) {
        yield.emplace_back(env, 0, m->capacity / i->time, ILOFLOAT, yield_name(m->name, i->name, t));
        setup.emplace_back(env, 0, 1, ILOBOOL, setup_name(m->name, i->name, t));
      }
    }
    for (const auto& i : in.items)
      for (const std::size_t t : in.periods) stock.emplace_back(env, 0, IloInfinity, ILOFLOAT, stock_name(i->name, t));
  });
  r.measure("constraints", [&] {
    for (std::size_t i = 0; i < in.items.size(); ++i) {
      for (std::size_t t = 0; t < t_count; ++t) {
        IloExpr in_flow(env);
        for (std::uint32_t k = routes->item_start[i]; k < routes->item_start[i + 1]; ++k)
          in_flow += yield[routes->of_item[k] * t_count + t];
        if (t > 0) in_flow += stock[i * t_count + t - 1];
        model.add(in_flow - in.items[i]->demand[t] == stock[i * t_count + t]);
        in_flow.end();
      }
    }
    for (std::size_t m = 0; m < in.machines.size(); ++m) {
      const std::uint32_t first = routes->machine_start[m], last = routes->machine_start[m + 1];
      if (first == last) continue;
      for (std::size_t t = 0; t < t_count; ++t) {
        IloExpr load(env);
        for (std::uint32_t q = first; q < last; ++q) load += routes->item[q]->time * yield[q * t_count + t];
        model.add(load <= in.machines[m]->capacity);
        load.end();
      }
    }
    for (std::size_t v = 0; v < yield.size(); ++v) model.add(yield[v] <= yield[v].getUB() * setup[v]);
    IloExpr cost(env);
    for (std::size_t i = 0; i < in.items.size(); ++i)
      for (std::size_t t = 0; t < t_count; ++t) cost += in.items[i]->hold * stock[i * t_count + t];
    for (std::size_t q = 0; q < routes->item.size(); ++q)
      for (std::size_t t = 0; t < t_count; ++t) cost += routes->machine[q]->setup * setup[q * t_count + t];
    model.add(IloMinimize(env, cost));
    cost.end();
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
  std::size_t items = 0, machines = 20, periods = 50;
  std::uint32_t rep       = 0;
  std::uint32_t seed      = 7;
  std::uint64_t heap_seed = 0;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--impl")
      impl = v;
    else if (k == "--items")
      items = std::strtoull(v.c_str(), nullptr, 10);
    else if (k == "--machines")
      machines = std::strtoull(v.c_str(), nullptr, 10);
    else if (k == "--periods")
      periods = std::strtoull(v.c_str(), nullptr, 10);
    else if (k == "--rep")
      rep = static_cast<std::uint32_t>(std::stoul(v));
    else if (k == "--seed")
      seed = static_cast<std::uint32_t>(std::stoul(v));
    else if (k == "--heap-seed")
      heap_seed = std::strtoull(v.c_str(), nullptr, 10);
    else if (k == "--pmu")
      pmu_spec = v;
    else
      usage("unknown option " + k);
  }
  if (impl != "dreamor" && impl != "plain" && impl != "flat") usage("--impl dreamor|plain|flat");
  if (items == 0 || machines == 0 || periods == 0) usage("--items, --machines and --periods must be positive");

  Result r;
  std::optional<Instance> in;
  r.setup("heap", [&] { preconditionHeap(heap_seed); });
  r.setup("instance", [&] { in.emplace(Instance::make(machines, items, periods, seed)); });
  std::size_t routes = 0;
  for (const auto& m : in->machines) routes += m->items.size();
  if (!pmu_spec.empty()) pmu().open(pmu_spec);
  if (impl == "dreamor")
    runDreamor(*in, r);
  else if (impl == "plain")
    runPlain(*in, r);
  else
    runFlat(*in, r);
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
