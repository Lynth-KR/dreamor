// SPDX-License-Identifier: Apache-2.0
// EntityVector: each entity once by its primary key; filled, sorted, frozen, then read; elements that
// never move once frozen, const access with modify() the one way to change one, and its use as a set
// source - where a missing keyName() leaves the primary key to name the variables.
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

using cMachine = dreamor::Col<const Machine*, "machine">;
using cPeriod  = dreamor::Col<std::size_t, "period">;

static_assert(dreamor::Entity<Machine> && dreamor::Entity<Route> && !dreamor::Entity<int>);
static_assert(!std::is_copy_constructible_v<dreamor::EntityVector<Machine>> &&
              std::is_nothrow_move_constructible_v<dreamor::EntityVector<Machine>>);

int main() try {
  // insert and emplace: the element with the key, by its address, and whether it went in - as std::set does
  dreamor::EntityVector<Machine> machines;
  machines.reserve(2000);
  const auto [m1, m1_new] = machines.emplace("M1", 40.0);
  DREAMOR_CHECK(m1_new && m1->code == "M1" && machines.size() == 1);
  const auto [again, again_new] = machines.insert(Machine{"M1", 99.0});
  DREAMOR_CHECK(!again_new && again == m1 && exactlyEqual(again->capacity, 40) && machines.size() == 1);
  DREAMOR_CHECK(machines.insert(Machine{"M2", 25.0}).second && machines.size() == 2);

  // within the capacity reserved, an element stays where it is, as a std::vector's does
  for (std::size_t i = 3; i < 2000; ++i) {
    machines.emplace("M" + std::to_string(i), 1.0);
  }
  DREAMOR_CHECK(m1->code == "M1" && machines.size() == 1999);
  machines.freeze();
  DREAMOR_CHECK(machines.frozen() && &machines.front() == m1);

  // by key, and in insertion order
  DREAMOR_CHECK(machines.contains("M2") && !machines.contains("M0") && machines.find("M0") == nullptr);
  DREAMOR_CHECK(machines.find("M2")->code == "M2" && machines[1].code == "M2" && machines.back().code == "M1999");
  bool all_found = true;
  for (const auto& m : machines) {
    all_found = all_found && machines.find(m.code) == &m;
  }
  DREAMOR_CHECK(all_found);

  // modify: the one way to change an element; the address stays
  const Machine* first = &machines.front();
  machines.modify(first, [](Machine& m) { m.load = 12.5; });
  DREAMOR_CHECK(exactlyEqual(machines.front().load, 12.5) && &machines.front() == first);

  // moving keeps every element where it is
  const dreamor::EntityVector<Machine> moved{std::move(machines)};
  DREAMOR_CHECK(&moved.front() == first && moved.size() == 1999 && moved.frozen());

  // sort(): primary-key order, whatever order the data came in; find follows the elements
  dreamor::EntityVector<Machine> sorted;
  for (const char* code : {"M3", "M10", "M1", "M2"}) {
    sorted.emplace(code, 1.0);
  }
  sorted.sort().freeze();
  DREAMOR_CHECK(sorted[0].code == "M1" && sorted[1].code == "M10" && sorted[2].code == "M2" && sorted[3].code == "M3");
  DREAMOR_CHECK(sorted.find("M2") == &sorted[2] && sorted.find("M10") == &sorted[1] && !sorted.contains("M4"));

  // sort(comp): an order of the caller's, as std::sort takes it - here the codes' numbers
  dreamor::EntityVector<Machine> natural;
  for (const char* code : {"M3", "M10", "M1", "M2"}) {
    natural.emplace(code, 1.0);
  }
  const auto number = [](const Machine& m) { return std::stoul(m.code.substr(1)); };
  natural.sort([&number](const Machine& a, const Machine& b) { return number(a) < number(b); }).freeze();
  DREAMOR_CHECK(natural[0].code == "M1" && natural[1].code == "M2" && natural[2].code == "M3" &&
                natural[3].code == "M10");
  DREAMOR_CHECK(natural.find("M10") == &natural[3]);

  // a composite key
  dreamor::EntityVector<Route> routes;
  DREAMOR_CHECK(routes.emplace(std::size_t{1}, "R").second && routes.emplace(std::size_t{2}, "R").second);
  DREAMOR_CHECK(!routes.emplace(std::size_t{1}, "R").second && routes.size() == 2);
  routes.freeze();
  DREAMOR_CHECK(routes.contains(std::tuple{std::size_t{2}, std::string{"R"}}));

  // a set source like any container: pointer columns, and the primary key names the variables
  // when there is no keyName()
  dreamor::EntityVector<Machine> two;
  two.emplace("A", 1.0);
  two.emplace("B", 2.0);
  two.freeze();
  const std::vector<std::size_t> periods{1, 2, 3};
  const auto sRUN = dreamor::makeSet<cMachine, cPeriod>(two, periods).collect();
  DREAMOR_CHECK(sRUN.size() == 6 && std::get<cMachine>(*sRUN.begin()).get() == &two.front());
  IloEnv env;
  const dreamor::concert::VarTable<cMachine, cPeriod> vRun{env, sRUN, 0, 1, ILOFLOAT, "run"};
  DREAMOR_CHECK(std::string{vRun(cMachine{&two.back()}, cPeriod{3}).getName()} == "run_B_3");

  // a set of the whole EntityVector, in order: an entity's row by its address. Each lookup gives what
  // the row gives, on that set and on one that skips an entity (looked up as before), and an entity of
  // another EntityVector, or one the set skips, is not in it
  dreamor::EntityVector<Machine> many;
  for (std::size_t i = 0; i < 100; ++i) {
    many.emplace("M" + std::to_string(i), static_cast<double>(i));
  }
  many.freeze();
  const auto sALL  = dreamor::makeSet<cMachine>(many).collect();
  const auto sSOME = dreamor::makeSet<cMachine>(many).where([](cMachine m) { return m->code != "M50"; }).collect();
  const dreamor::ValueTable<cMachine> capAll{sALL, [](cMachine m) { return m->capacity; }};
  const dreamor::ValueTable<cMachine> capSome{sSOME, [](cMachine m) { return m->capacity; }};
  bool byAddress = true;
  for (const Machine& m : many) {
    byAddress = byAddress && exactlyEqual(capAll(cMachine{&m}), m.capacity);
    byAddress = byAddress &&
                (m.code == "M50" ? !capSome.contains(cMachine{&m}) : exactlyEqual(capSome(cMachine{&m}), m.capacity));
  }
  DREAMOR_CHECK(byAddress);
  DREAMOR_CHECK(!capAll.contains(cMachine{&two.front()}) && !capAll.contains(cMachine{nullptr}));
  env.end();
  return report("entity_vector");
} catch (...) { return ::uncaught(); }
