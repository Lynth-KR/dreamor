// SPDX-License-Identifier: Apache-2.0
// EntityVector: each entity once by its primary key, elements that never move, const access with
// modify() the one way to change one, and its use as a set source - where a missing keyName()
// leaves the primary key to name the variables.
#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"

namespace {

struct Machine {
  std::string code;
  double capacity = 0;
  double load     = 0;  // a result written back
};
inline const std::string& primaryKey(const Machine& m) {
  return m.code;
}

struct Route {  // a composite key: the same code at two plants is two routes
  std::size_t plant;
  std::string code;
};
inline auto primaryKey(const Route& r) {
  return std::tuple{r.plant, r.code};
}

}  // namespace

using MachineCol = dreamor::Col<const Machine*, "machine">;
using PeriodCol  = dreamor::Col<std::size_t, "period">;

static_assert(dreamor::Entity<Machine> && dreamor::Entity<Route> && !dreamor::Entity<int>);
static_assert(!std::is_copy_constructible_v<dreamor::EntityVector<Machine>> &&
              std::is_nothrow_move_constructible_v<dreamor::EntityVector<Machine>>);

int main() try {
  // insert and emplace: the element with the key, by its address, and whether it went in - as std::set does
  dreamor::EntityVector<Machine> machines;
  const auto [m1, m1_new] = machines.emplace("M1", 40.0);
  DREAMOR_CHECK(m1_new && m1->code == "M1" && machines.size() == 1);
  const auto [again, again_new] = machines.insert(Machine{"M1", 99.0});
  DREAMOR_CHECK(!again_new && again == m1 && exactlyEqual(again->capacity, 40) && machines.size() == 1);
  DREAMOR_CHECK(machines.insert(Machine{"M2", 25.0}).second && machines.size() == 2);

  // by key, and in insertion order
  DREAMOR_CHECK(machines.contains("M2") && !machines.contains("M3") && machines.find("M3") == nullptr);
  DREAMOR_CHECK(machines.find("M2")->code == "M2" && machines[0].code == "M1" && machines.back().code == "M2");

  // elements never move: a pointer taken early still points at its entity after many more
  const Machine* first = &machines.front();
  for (std::size_t i = 3; i < 2000; ++i) {
    machines.emplace("M" + std::to_string(i), 1.0);
  }
  DREAMOR_CHECK(&machines.front() == first && first->code == "M1" && machines.size() == 1999);
  DREAMOR_CHECK(m1 == first && again == first);  // what emplace and insert returned lasts too
  bool all_found = true;
  for (const auto& m : machines) {
    all_found = all_found && machines.find(m.code) == &m;
  }
  DREAMOR_CHECK(all_found);

  // modify: the one way to change an element; the address stays
  machines.modify(first, [](Machine& m) { m.load = 12.5; });
  DREAMOR_CHECK(exactlyEqual(machines.front().load, 12.5) && &machines.front() == first);

  // moving keeps every element where it is
  const dreamor::EntityVector<Machine> moved{std::move(machines)};
  DREAMOR_CHECK(&moved.front() == first && moved.size() == 1999);

  // a composite key
  dreamor::EntityVector<Route> routes;
  DREAMOR_CHECK(routes.emplace(std::size_t{1}, "R").second && routes.emplace(std::size_t{2}, "R").second);
  DREAMOR_CHECK(!routes.emplace(std::size_t{1}, "R").second && routes.size() == 2);
  DREAMOR_CHECK(routes.contains(std::tuple{std::size_t{2}, std::string{"R"}}));

  // a set source like any container: pointer columns, and the primary key names the variables
  // when there is no keyName()
  dreamor::EntityVector<Machine> two;
  two.emplace("A", 1.0);
  two.emplace("B", 2.0);
  const std::vector<std::size_t> periods{1, 2, 3};
  const auto sRUN = dreamor::makeSet<MachineCol, PeriodCol>(two, periods).collect();
  DREAMOR_CHECK(sRUN.size() == 6 && std::get<MachineCol>(*sRUN.begin()).get() == &two.front());
  IloEnv env;
  const dreamor::concert::VarTable<MachineCol, PeriodCol> vRun{env, sRUN, 0, 1, ILOFLOAT, "run"};
  DREAMOR_CHECK(std::string{vRun(MachineCol{&two.back()}, PeriodCol{3}).getName()} == "run_B_3");
  env.end();
  return report("entity_vector");
} catch (...) { return ::uncaught(); }
