// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A column under std::format prints its name token - keyName(), else primaryKey(), else the
// value - and a column with no token has no formatter. An entity column may be declared before its
// entity is complete.
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "dreamor/index_set.hpp"

namespace {

namespace keyed {
struct Machine {
  std::string name;
};
inline const std::string& keyName(const Machine* m) {
  return m->name;
}
struct Node {
  std::uint32_t id;
};
inline std::uint32_t keyName(const Node* n) {
  return n->id;
}
}  // namespace keyed

namespace stored {
struct Item {
  std::string code;
};
inline const std::string& primaryKey(const Item& i) {
  return i.code;
}
}  // namespace stored

namespace bare {
struct Thing {};
}  // namespace bare

// an entity born later in the file: its column is declared while it is still incomplete
namespace late {
struct Batch;
}  // namespace late

}  // namespace

using cBatch = dreamor::Col<const late::Batch*, "batch">;

namespace {
namespace late {
struct Batch {
  std::uint32_t number;
};
inline std::uint32_t keyName(const Batch* b) {
  return b->number;
}
}  // namespace late
}  // namespace

using cMachine = dreamor::Col<const keyed::Machine*, "machine">;
using cNode    = dreamor::Col<const keyed::Node*, "node">;
using cItem    = dreamor::Col<const stored::Item*, "item">;
using cThing   = dreamor::Col<const bare::Thing*, "thing">;
using cPeriod  = dreamor::Col<std::uint32_t, "period">;
using cGrade   = dreamor::Col<std::string, "grade">;

template <typename T>
constexpr bool kFormattable = std::is_default_constructible_v<std::formatter<T, char>>;

// an entity pointer with neither hook has no token, so no formatter
static_assert(kFormattable<cMachine> && kFormattable<cNode> && kFormattable<cItem> && kFormattable<cPeriod> &&
              kFormattable<cGrade>);
static_assert(!kFormattable<cThing>);
static_assert(kFormattable<cBatch>);

int main() try {
  const keyed::Machine m3{"M3"};
  const keyed::Node n7{7};
  const std::vector<stored::Item> items{{"I-01"}};

  // keyName() hooks (text and a number), primaryKey(), and plain values
  DREAMOR_CHECK(std::format("{}", cMachine{&m3}) == "M3");
  DREAMOR_CHECK(std::format("{}", cNode{&n7}) == "7");
  DREAMOR_CHECK(std::format("{}", cItem{items.data()}) == "I-01");
  DREAMOR_CHECK(std::format("{}", cPeriod{2}) == "2");
  DREAMOR_CHECK(std::format("{}", cGrade{"A"}) == "A");

  // a column declared before its entity was complete names it by the keyName defined with the entity
  const late::Batch b12{12};
  DREAMOR_CHECK(std::format("{}", cBatch{&b12}) == "12");
  DREAMOR_CHECK(std::string{dreamor::exprName("Run", cBatch{&b12}, cPeriod{2})} == "Run_12_2");

  // a row name, the way VarTable names a variable
  DREAMOR_CHECK(std::format("Cap_{}_{}_{}", cMachine{&m3}, cNode{&n7}, cPeriod{2}) == "Cap_M3_7_2");

  // exprName: the same name with no format string, from columns and plain values alike; one buffer of
  // the thread's for every exprName, so a name lives until the next
  const cMachine machine{&m3};
  const cNode node{&n7};
  const cPeriod second{2};
  DREAMOR_CHECK(std::string{dreamor::exprName("Cap", machine, node, second)} == "Cap_M3_7_2");
  DREAMOR_CHECK(std::string{dreamor::exprName("Stock", cItem{items.data()}, cGrade{"A"})} == "Stock_I-01_A");
  DREAMOR_CHECK(std::string{dreamor::exprName("Tangent", node, std::size_t{3})} == "Tangent_7_3");
  DREAMOR_CHECK(std::string{dreamor::exprName("Objective")} == "Objective");
  const char* first = dreamor::exprName("A", node);
  DREAMOR_CHECK(dreamor::exprName("B", machine, second) == first && std::string{first} == "B_M3_2");
  const std::string tooLong(300, 'x');
  bool overflowed = false;
  try {
    (void)dreamor::exprName(tooLong, node);
  } catch (const std::runtime_error&) { overflowed = true; }
  DREAMOR_CHECK(overflowed);

  // a column takes no format spec
  bool rejected = false;
  try {
    const cPeriod period{2};
    (void)std::vformat("{:>4}", std::make_format_args(period));
  } catch (const std::format_error&) { rejected = true; }
  DREAMOR_CHECK(rejected);

  return report("name_token");
} catch (...) { return ::uncaught(); }
