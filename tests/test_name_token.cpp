// SPDX-License-Identifier: Apache-2.0
// A column under std::format prints its name token - keyName(), else primaryKey(), else the
// value - and a column with no token has no formatter.
#include <cstdint>
#include <format>
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

}  // namespace

using MachineCol = dreamor::Col<const keyed::Machine*, "machine">;
using NodeCol    = dreamor::Col<const keyed::Node*, "node">;
using ItemCol    = dreamor::Col<const stored::Item*, "item">;
using ThingCol   = dreamor::Col<const bare::Thing*, "thing">;
using PeriodCol  = dreamor::Col<std::uint32_t, "period">;
using GradeCol   = dreamor::Col<std::string, "grade">;

template <typename T>
constexpr bool kFormattable = std::is_default_constructible_v<std::formatter<T, char>>;

// an entity pointer with neither hook has no token, so no formatter
static_assert(kFormattable<MachineCol> && kFormattable<NodeCol> && kFormattable<ItemCol> && kFormattable<PeriodCol> &&
              kFormattable<GradeCol>);
static_assert(!kFormattable<ThingCol>);

int main() try {
  const keyed::Machine m3{"M3"};
  const keyed::Node n7{7};
  const std::vector<stored::Item> items{{"I-01"}};

  // keyName() hooks (text and a number), primaryKey(), and plain values
  DREAMOR_CHECK(std::format("{}", MachineCol{&m3}) == "M3");
  DREAMOR_CHECK(std::format("{}", NodeCol{&n7}) == "7");
  DREAMOR_CHECK(std::format("{}", ItemCol{items.data()}) == "I-01");
  DREAMOR_CHECK(std::format("{}", PeriodCol{2}) == "2");
  DREAMOR_CHECK(std::format("{}", GradeCol{"A"}) == "A");

  // a row name, the way VarTable names a variable
  DREAMOR_CHECK(std::format("Cap_{}_{}_{}", MachineCol{&m3}, NodeCol{&n7}, PeriodCol{2}) == "Cap_M3_7_2");

  // a column takes no format spec
  bool rejected = false;
  try {
    const PeriodCol period{2};
    (void)std::vformat("{:>4}", std::make_format_args(period));
  } catch (const std::format_error&) { rejected = true; }
  DREAMOR_CHECK(rejected);

  return report("name_token");
} catch (...) { return ::uncaught(); }
