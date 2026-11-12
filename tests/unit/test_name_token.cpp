// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// What a value names as, under std::format and in indexedName - as a VarTable names its variables: a column
// by its value, a Ref by its entity, a char as the character, an enum as its number, an entity by its primary
// key (a member first, then a free function), a composite key by its parts joined by "_" or by
// primaryKeySeparator(), a number or a text as it is, anything else through its std::formatter. A value that
// names by none of these cannot be a key.
// An entity column may be declared before its entity is complete.
#include <cstdint>
#include <format>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

namespace keyed {
struct Machine {  // the key by a member
  std::string name;

  [[nodiscard]] const std::string& primaryKey() const { return name; }
};

struct Node {  // the key by a free function
  std::uint32_t id;
};

inline std::uint32_t primaryKey(const Node& n) {
  return n.id;
}

struct Route {  // a composite key of references: "M3_7"
  dreamor::Ref<Machine> machine;
  dreamor::Ref<Node> node;

  [[nodiscard]] auto primaryKey() const { return std::tuple{machine, node}; }
};

struct Arc {  // a composite key with its own separator: "M3->M4"
  dreamor::Ref<Machine> from;
  dreamor::Ref<Machine> to;

  [[nodiscard]] auto primaryKey() const { return std::tuple{from, to}; }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static): the hook is found as a member call
  [[nodiscard]] std::string_view primaryKeySeparator() const { return "->"; }
};
}  // namespace keyed

struct Shade {  // no key, but a formatter of its own
  std::uint32_t level;
};

namespace bare {
struct Thing {};
}  // namespace bare

// an entity born later in the file: its column is declared while it is still incomplete
namespace late {
struct Batch;
}  // namespace late

}  // namespace

template <>
struct std::formatter<Shade, char> {
  static constexpr auto parse(std::format_parse_context& context) { return context.begin(); }

  template <typename FormatContext>
  static auto format(const Shade& shade, FormatContext& context) {
    return std::format_to(context.out(), "s{}", shade.level);
  }
};

namespace {
DREAMOR_ENTITY_COLUMN(cBatch, late::Batch);
}  // namespace

namespace {
namespace late {
struct Batch {
  std::uint32_t number;

  [[nodiscard]] std::uint32_t primaryKey() const { return number; }
};
}  // namespace late
}  // namespace

namespace {
DREAMOR_ENTITY_COLUMN(cMachine, keyed::Machine);
DREAMOR_ENTITY_COLUMN(cNode, keyed::Node);
DREAMOR_ENTITY_COLUMN(cRoute, keyed::Route);
DREAMOR_ENTITY_COLUMN(cArc, keyed::Arc);
DREAMOR_VALUE_COLUMN(cScenario, std::int32_t);
DREAMOR_VALUE_COLUMN(cPeriod, std::uint32_t);
DREAMOR_VALUE_COLUMN(cGrade, std::string_view);
enum class Shift : char { kDay = 'D', kNight = 'N' };  // no formatter of its own, over char
DREAMOR_VALUE_COLUMN(cShift, Shift);
}  // namespace

template <typename T>
constexpr bool kFormattable = std::is_default_constructible_v<std::formatter<T, char>>;

// every column names; a value with no key, no formatter and that is neither a number nor a text names as
// nothing, so it cannot be a key
static_assert(kFormattable<cMachine> && kFormattable<cNode> && kFormattable<cRoute> && kFormattable<cArc> &&
              kFormattable<cScenario> && kFormattable<cPeriod> && kFormattable<cGrade>);
static_assert(dreamor::internal::nameable<Shade> && !dreamor::internal::nameable<bare::Thing>);
static_assert(kFormattable<cBatch>);

int main() try {
  dreamor::EntityVector<keyed::Machine> machines;
  static_cast<void>(machines.emplace(keyed::Machine{"M3"}));
  static_cast<void>(machines.emplace(keyed::Machine{"M4"}));
  const std::string long_code(300, 'L');  // longer than a solver's name may be
  static_cast<void>(machines.emplace(keyed::Machine{long_code}));
  machines.freeze();
  dreamor::EntityVector<keyed::Node> nodes;
  static_cast<void>(nodes.emplace(keyed::Node{7}));
  nodes.freeze();
  dreamor::EntityVector<keyed::Route> routes;
  static_cast<void>(routes.emplace(keyed::Route{machines.at("M3"), nodes.at(7)}));
  routes.freeze();
  dreamor::EntityVector<keyed::Arc> arcs;
  static_cast<void>(arcs.emplace(keyed::Arc{machines.at("M3"), machines.at("M4")}));
  arcs.freeze();
  dreamor::EntityVector<late::Batch> batches;
  static_cast<void>(batches.emplace(late::Batch{12}));
  batches.freeze();

  const cMachine m3{machines.at("M3")};
  const cNode n7{nodes.at(7)};
  const cRoute route{routes.at(std::tuple{machines.at("M3"), nodes.at(7)})};
  const cArc arc{arcs.at(std::tuple{machines.at("M3"), machines.at("M4")})};

  // primary keys (by a member and by a free function), composite keys, strong types, formatters, plain values
  DREAMOR_CHECK(std::format("{}", m3) == "M3");
  DREAMOR_CHECK(std::format("{}", n7) == "7");
  DREAMOR_CHECK(std::format("{}", route) == "M3_7");
  DREAMOR_CHECK(std::format("{}", arc) == "M3->M4");
  DREAMOR_CHECK(std::format("{}", cScenario{5}) == "5");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Tint", Shade{2})} == "Tint_s2");
  DREAMOR_CHECK(std::format("{}", cPeriod{2}) == "2");
  DREAMOR_CHECK(std::format("{}", cGrade{"A"}) == "A");
  // std::format writes a name of any length: only a name a solver takes is held to 255 characters
  DREAMOR_CHECK(std::format("Cap_{}", cMachine{machines.at(long_code)}) == "Cap_" + long_code);

  // a column declared before its entity was complete names it by the key defined with the entity
  const cBatch b12{batches.at(12)};
  DREAMOR_CHECK(std::format("{}", b12) == "12");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Run", b12, cPeriod{2})} == "Run_12_2");

  // a row name, the way VarTable names a variable
  DREAMOR_CHECK(std::format("Cap_{}_{}_{}", m3, n7, cPeriod{2}) == "Cap_M3_7_2");

  // indexedName: the same name with no format string, from columns and plain values alike; one buffer of
  // the thread's for every indexedName, so a name lives until the next
  const cPeriod second{2};
  DREAMOR_CHECK(std::string{dreamor::indexedName("Cap", m3, n7, second)} == "Cap_M3_7_2");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Use", route, cScenario{1})} == "Use_M3_7_1");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Flow", arc)} == "Flow_M3->M4");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Stock", n7, cGrade{"A"})} == "Stock_7_A");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Tangent", n7, std::size_t{3})} == "Tangent_7_3");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Objective")} == "Objective");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Pair", std::tuple{std::uint32_t{1}, std::string{"a"}})} ==
                "Pair_1_a");
  const char* first = dreamor::indexedName("A", n7);
  DREAMOR_CHECK(dreamor::indexedName("B", m3, second) == first && std::string{first} == "B_M3_2");
  // 255 characters is the longest name: "F_7" and 252 more
  DREAMOR_CHECK(std::string_view{dreamor::indexedName(std::string(253, 'F'), n7)}.size() == 255);
  bool oneMore = false;
  try {
    (void)dreamor::indexedName(std::string(254, 'F'), n7);
  } catch (const std::runtime_error&) { oneMore = true; }
  DREAMOR_CHECK(oneMore);
  const std::string tooLong(300, 'x');
  bool overflowed = false;
  try {
    (void)dreamor::indexedName(tooLong, n7);
  } catch (const std::runtime_error&) { overflowed = true; }
  DREAMOR_CHECK(overflowed);

  // under a format spec, a column of numbers or text prints as its value does; any other is named, then laid out
  const cPeriod period{2};
  const cScenario back{-3};
  const cGrade grade{"A"};
  DREAMOR_CHECK(std::format("{:>4}|{:04}|{:<3}|{:+}", period, period, grade, back) == "   2|0002|A  |-3");
  DREAMOR_CHECK(std::format("{:<4}|{:>5}|{}", m3, route, m3) == "M3  | M3_7|M3");
  // a Ref prints as its entity names, and both print on a stream as under "{}"
  DREAMOR_CHECK(std::format("{}|{:>3}", machines.at("M4"), machines.at("M3")) == "M4| M3");
  std::ostringstream printed;
  printed << m3 << ' ' << period << ' ' << grade << ' ' << machines.at("M4");
  DREAMOR_CHECK(printed.str() == "M3 2 A M4");

  // An enum without a formatter of its own is its number, in a name and under std::format alike, even over char,
  // which std::format would otherwise write as the character.
  const cShift day{Shift::kDay};
  DREAMOR_CHECK(std::string_view{dreamor::indexedName("x", day)} == "x_68");
  DREAMOR_CHECK(std::format("{}", day) == "68" && std::format("{:>4}", day) == "  68");

  return report("name_token");
} catch (...) { return ::uncaught(); }
