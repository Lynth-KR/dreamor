// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// EntityVector: each entity once by its primary key, found member-first; filled, sorted, frozen, then read;
// elements that never move once frozen; references by key (at, find) as Ref; and its use as a set source, the
// primary key naming the variables; and a sort whose index cannot be allocated, or whose comparison throws.
#include <compare>
#include <cstddef>
#include <cstdint>
#include <new>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"
#include "support/failing_allocation.hpp"
#include "support/float_compare.hpp"

namespace {

struct Machine {  // the key by a member
  std::string code;
  double capacity = 0;

  [[nodiscard]] const std::string& primaryKey() const { return code; }
};

struct Plant {  // the key by a free function, as for a type one cannot change
  std::size_t id;
};

inline std::size_t primaryKey(const Plant& p) {
  return p.id;
}

struct Route {  // a composite key: the same code at two plants is two routes
  dreamor::Ref<Plant> plant;
  std::string code;

  [[nodiscard]] auto primaryKey() const { return std::tuple{plant, code}; }
};

}  // namespace

namespace {
DREAMOR_ENTITY_COLUMN(cMachine, Machine);
DREAMOR_VALUE_COLUMN(cPeriod, std::size_t);
DREAMOR_VALUE_COLUMN(cCode, std::string_view);
}  // namespace

static_assert(dreamor::entity<Machine> && dreamor::entity<Plant> && dreamor::entity<Route> && !dreamor::entity<int>);
static_assert(!std::is_copy_constructible_v<dreamor::EntityVector<Machine>> &&
              std::is_nothrow_move_constructible_v<dreamor::EntityVector<Machine>>);
static_assert(std::is_same_v<decltype(std::declval<dreamor::EntityVector<Machine>&>().emplace("M", 1.0)), bool>);
static_assert(!std::is_constructible_v<dreamor::Ref<Machine>, const Machine*> &&
              !std::is_constructible_v<dreamor::Ref<Machine>, const Machine&>);

int main() try {
  // insert and emplace: whether it went in - a repeated key is refused, as a table refuses the INSERT
  dreamor::EntityVector<Machine> machines;
  machines.reserve(2000);
  DREAMOR_CHECK(machines.emplace("M1", 40.0) && machines.size() == 1);
  DREAMOR_CHECK(!machines.insert(Machine{"M1", 99.0}) && machines.size() == 1);
  DREAMOR_CHECK(machines.insert(Machine{"M2", 25.0}) && machines.size() == 2);
  for (std::size_t i = 3; i < 2000; ++i) {
    static_cast<void>(machines.emplace("M" + std::to_string(i), 1.0));
  }
  machines.freeze();
  DREAMOR_CHECK(machines.frozen() && machines.size() == 1999 && exactlyEqual(machines.at("M1")->capacity, 40));

  // by key: at() refers or throws, find() refers or is empty; by position, [i], in insertion order
  const dreamor::Ref<Machine> m1 = machines.at("M1");
  DREAMOR_CHECK(machines.contains("M2") && !machines.contains("M0") && !machines.find("M0"));
  DREAMOR_CHECK(machines.find("M2")->code == "M2" && machines[1].code == "M2" && machines.back().code == "M1999");
  DREAMOR_CHECK(m1 == machines.find("M1") && m1 != machines.find("M2") &&
                machines.find("M0") == dreamor::Ref<Machine>{});
  std::string missing;
  try {
    static_cast<void>(machines.at("M0"));
  } catch (const std::out_of_range& error) { missing = error.what(); }
  DREAMOR_CHECK(missing == "EntityVector::at: no element has the key (M0).");
  // a key of text by any text: a std::string_view, a column of text, a literal - no std::string made
  const std::string_view m2_code = "M2";
  DREAMOR_CHECK(machines.at(m2_code) == machines.at(std::string{"M2"}) && machines.contains(cCode{m2_code}) &&
                !machines.find(std::string_view{"M0"}) && machines.find(cCode{"M1999"})->code == "M1999");
  bool all_found = true;
  for (const auto& m : machines) {
    all_found = all_found && &*machines.at(m.code) == &m;
  }
  DREAMOR_CHECK(all_found);

  // moving keeps every element where it is, so a Ref still refers
  const dreamor::EntityVector<Machine> moved{std::move(machines)};
  DREAMOR_CHECK(&moved.front() == &*m1 && moved.size() == 1999 && moved.frozen() && moved.at("M1") == m1);

  // sort(): primary-key order, whatever order the data came in; references follow the elements
  dreamor::EntityVector<Machine> sorted;
  for (const char* code : {"M3", "M10", "M1", "M2"}) {
    static_cast<void>(sorted.emplace(code, 1.0));
  }
  sorted.sort().freeze();
  DREAMOR_CHECK(sorted[0].code == "M1" && sorted[1].code == "M10" && sorted[2].code == "M2" && sorted[3].code == "M3");
  DREAMOR_CHECK(&*sorted.at("M2") == &sorted[2] && &*sorted.at("M10") == &sorted[1] && !sorted.contains("M4"));
  // Refs order by the primary key, not by address
  DREAMOR_CHECK(sorted.at("M1") < sorted.at("M10") && sorted.at("M2") < sorted.at("M3") &&
                dreamor::Ref<Machine>{} < sorted.at("M1"));
  // ref: the Ref of an element at hand, as a loop gives it, with no lookup of its key; refs: the elements as Refs
  for (const Machine& m : sorted) {
    DREAMOR_CHECK(sorted.ref(m) == sorted.at(m.code) && &*sorted.ref(m) == &m);
  }
  std::size_t position = 0;
  for (const dreamor::Ref<Machine> m : sorted.refs()) {
    DREAMOR_CHECK(&*m == &sorted[position]);
    ++position;
  }
  DREAMOR_CHECK(position == sorted.size() && std::ranges::size(sorted.refs()) == sorted.size());
  // const iteration, and a Ref's hash: by the entity, so that the same entity hashes alike from any Ref
  DREAMOR_CHECK(sorted.cbegin() == sorted.begin() && sorted.cend() == sorted.end());
  const std::hash<dreamor::Ref<Machine>> hash;
  const std::unordered_set<dreamor::Ref<Machine>> seen{sorted.at("M1"), sorted.at("M2"), sorted.at("M1")};
  DREAMOR_CHECK(hash(sorted.at("M2")) == hash(sorted.at("M2")) && seen.size() == 2 && seen.contains(sorted.at("M1")));
  // entities of two vectors with one key: equivalent, but not equal
  DREAMOR_CHECK((moved.at("M1") <=> sorted.at("M1")) == std::weak_ordering::equivalent &&
                moved.at("M1") != sorted.at("M1"));

  // sort(comp): an order of the caller's, as std::sort takes it - here the codes' numbers
  dreamor::EntityVector<Machine> natural;
  for (const char* code : {"M3", "M10", "M1", "M2"}) {
    static_cast<void>(natural.emplace(code, 1.0));
  }
  const auto number    = [](const Machine& m) { return std::stoul(m.code.substr(1)); };
  const auto by_number = [&number](const Machine& a, const Machine& b) { return number(a) < number(b); };
  natural.sort(by_number).freeze();
  DREAMOR_CHECK(natural[0].code == "M1" && natural[1].code == "M2" && natural[2].code == "M3" &&
                natural[3].code == "M10");
  DREAMOR_CHECK(&*natural.at("M10") == &natural[3]);

  // a composite key that refers to another entity: the plant comes first, frozen, then the routes
  dreamor::EntityVector<Plant> plants;
  DREAMOR_CHECK(plants.emplace(std::size_t{1}) && plants.emplace(std::size_t{2}));
  plants.freeze();
  dreamor::EntityVector<Route> routes;
  DREAMOR_CHECK(routes.emplace(plants.at(1), "R") && routes.emplace(plants.at(2), "R"));
  DREAMOR_CHECK(!routes.emplace(plants.at(1), "R") && routes.size() == 2);
  routes.freeze();
  DREAMOR_CHECK(routes.contains(std::tuple{plants.at(2), std::string{"R"}}) &&
                routes.at(std::tuple{plants.at(1), std::string{"R"}})->plant == plants.at(1));

  // a set source: Ref columns, and the primary key names the variables
  dreamor::EntityVector<Machine> two;
  static_cast<void>(two.emplace("A", 1.0));
  static_cast<void>(two.emplace("B", 2.0));
  two.freeze();
  const std::vector<std::size_t> periods{1, 2, 3};
  const auto sRUN = dreamor::index_set::relational::from<cMachine>(two).cross<cPeriod>(periods).build();
  DREAMOR_CHECK(sRUN.size() == 6 && std::get<cMachine>(*sRUN.begin()).get() == two.at("A"));
  IloEnv env;
  const dreamor::concert::VarTable<cMachine, cPeriod> vRun{env, sRUN, 0, 1, ILOFLOAT, "run"};
  DREAMOR_CHECK(std::string{vRun(cMachine{two.at("B")}, cPeriod{3}).getName()} == "run_B_3");

  // a set of the whole EntityVector, in order: an entity's row by its address. Each lookup gives what
  // the row gives, on that set and on one that skips an entity (looked up as before), and an entity of
  // another EntityVector, or one the set skips, is not in it
  dreamor::EntityVector<Machine> many;
  for (std::size_t i = 0; i < 100; ++i) {
    static_cast<void>(many.emplace("M" + std::to_string(i), static_cast<double>(i)));
  }
  many.freeze();
  const auto not_m50     = [](cMachine m) { return m->code != "M50"; };
  const auto capacity_of = [](cMachine m) { return m->capacity; };
  const auto sALL        = dreamor::index_set::relational::from<cMachine>(many).build();
  const auto sSOME       = dreamor::index_set::relational::from<cMachine>(many).where(not_m50).build();
  const dreamor::ParamTable<double, cMachine> capAll{sALL, capacity_of};
  const dreamor::ParamTable<double, cMachine> capSome{sSOME, capacity_of};
  bool byAddress = true;
  for (const Machine& m : many) {
    const cMachine column{many.at(m.code)};
    byAddress = byAddress && exactlyEqual(capAll(column), m.capacity);
    byAddress = byAddress && (m.code == "M50" ? !capSome.contains(column) : exactlyEqual(capSome(column), m.capacity));
  }
  DREAMOR_CHECK(byAddress);
  DREAMOR_CHECK(!capAll.contains(cMachine{two.at("A")}) && !capAll.contains(cMachine{}));
  env.end();

  // A sort whose new index cannot be allocated leaves the vector as it was: its order, and every entity found.
  dreamor::EntityVector<Plant> unsorted;
  for (const std::size_t id : {5U, 4U, 3U, 2U, 1U}) {
    unsorted.insert(Plant{id});
  }
  failing_allocation::failNextOfSize(16 * sizeof(std::uint32_t));  // the new index: 16 slots for 5 entities
  DREAMOR_CHECK_THROWS(std::bad_alloc, static_cast<void>(unsorted.sort()));
  unsorted.freeze();
  bool as_it_was = unsorted.size() == 5 && unsorted.front().id == 5 && unsorted.back().id == 1;
  for (const std::size_t id : {1U, 2U, 3U, 4U, 5U}) {
    as_it_was = as_it_was && unsorted.at(id)->id == id;
  }
  DREAMOR_CHECK(as_it_was);

  // A comparison that throws while the entities move leaves the vector empty, and ready to be filled again.
  dreamor::EntityVector<Plant> thrown;
  for (const std::size_t id : {3U, 1U, 2U}) {
    thrown.insert(Plant{id});
  }
  const auto throwing = [](const Plant&, const Plant&) -> bool { throw std::runtime_error("comparison"); };
  DREAMOR_CHECK_THROWS(std::runtime_error, static_cast<void>(thrown.sort(throwing)));
  DREAMOR_CHECK(thrown.empty() && thrown.insert(Plant{7}));
  thrown.freeze();
  DREAMOR_CHECK(thrown.size() == 1 && thrown.contains(std::size_t{7}));

  return report("entity_vector");
} catch (...) { return ::uncaught(); }
