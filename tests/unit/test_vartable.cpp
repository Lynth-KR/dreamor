// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// VarTable: built once from a set, bounds as numbers or column callables, names from keys,
// lookup, iteration in set order, solution(); a table moved from or constructed
// with no arguments, empty, and a family moved into it once.
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace {
DREAMOR_VALUE_COLUMN(cI, std::size_t);
DREAMOR_VALUE_COLUMN(cJ, std::size_t);
DREAMOR_VALUE_COLUMN(cK, std::size_t);
}  // namespace

// An entity whose key's formatter throws: the variable name stops after the separator, with a warning,
// instead of the throw ending the noexcept naming in std::terminate.
namespace {

namespace throwing {
struct Token {
  std::uint32_t value;
  friend bool operator==(Token, Token) = default;
};

struct Holder {
  Token token;

  [[nodiscard]] Token primaryKey() const { return token; }
};
}  // namespace throwing

namespace named {
struct Thing {
  std::string name;
};

inline const std::string& primaryKey(const Thing& thing) {
  return thing.name;
}
}  // namespace named
}  // namespace

template <>
struct std::hash<throwing::Token> {
  [[nodiscard]] std::size_t operator()(const throwing::Token& token) const noexcept {
    return static_cast<std::size_t>(token.value);
  }
};

template <>
struct std::formatter<throwing::Token> {
  static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

  template <typename FormatContext>
  static auto format(const throwing::Token& /*token*/, FormatContext& ctx) -> decltype(ctx.out()) {
    throw std::runtime_error("this token cannot be formatted");
  }
};

namespace {
DREAMOR_ENTITY_COLUMN(cThing, throwing::Holder);
DREAMOR_ENTITY_COLUMN(cNamed, named::Thing);
}  // namespace

namespace {

// Stands in for IloCplex: getEnv() and getValues(IloNumArray, IloNumVarArray).
struct FakeAlgorithm {
  IloEnv env;

  [[nodiscard]] IloEnv getEnv() const { return env; }

  // "Solution" = lb + ub, so each value is checkable from the variable itself.
  static void getValues(IloNumArray out, const IloNumVarArray vars) {
    for (IloInt i = 0; i < vars.getSize(); ++i) {
      out.add(vars[i].getLB() + vars[i].getUB());
    }
  }
};

// As Concert's algorithm, where some of the variables were not extracted: getValues refuses the array, and
// isExtracted and getValue answer one by one. Here a variable is extracted when its upper bound is above 1.
struct PartlyExtracted {
  IloEnv env;

  [[nodiscard]] IloEnv getEnv() const { return env; }

  static void getValues(IloNumArray /*out*/, const IloNumVarArray /*vars*/) {
    throw IloException{};  // NOLINT(bugprone-std-exception-baseclass): Concert's own exception, as CPLEX throws it
  }

  [[nodiscard]] static bool isExtracted(const IloNumVar& var) { return var.getUB() > 1.0; }

  [[nodiscard]] static double getValue(const IloNumVar& var) { return var.getUB(); }
};

// An algorithm that refuses the array and cannot answer one by one: solution() throws its exception on.
struct Refusing {
  IloEnv env;

  [[nodiscard]] IloEnv getEnv() const { return env; }

  static void getValues(IloNumArray /*out*/, const IloNumVarArray /*vars*/) {
    throw IloException{};  // NOLINT(bugprone-std-exception-baseclass): Concert's own exception, as CPLEX throws it
  }
};

DREAMOR_VALUE_COLUMN(cPeriod, std::uint32_t);
DREAMOR_VALUE_COLUMN(cStart, std::uint32_t);
DREAMOR_VALUE_COLUMN(cCount, std::size_t);
DREAMOR_ENTITY_COLUMN(cFrom, named::Thing);
DREAMOR_ENTITY_COLUMN(cTo, named::Thing);

// A value in a key column's place where its type names that column alone in the key - any integer for a
// column of integers, a Ref for its entity's column, a column of the same type under another name, a row of
// one column; where two key columns share a type, only the columns themselves. A value no set has - one
// the column's type cannot hold - is a missing key.
// ParamTable{records, row_of, value_of}: each record's value on the row row_of makes of it, keys() the set
// IndexSet{records, row_of} builds - a number from a temporary, a text from records that outlive the table.
struct ArcRecord {
  std::size_t from, to;
  double cost;
  std::string label;
};

// A variable no row or objective uses reads as 0; the others as the algorithm has them.
void unextractedVariables(const IloEnv& env) {
  const std::vector<std::size_t> I{1, 2, 3};
  const auto sI       = dreamor::index_set::relational::from<cI>(I).build();
  const auto upper_of = [](cI i) { return static_cast<double>(i.get()); };  // x_1 has 1 as its bound: unused
  const dreamor::concert::VarTable<cI> x{env, sI, 0, upper_of, ILOFLOAT, "x"};
  const auto plan = x.solution(PartlyExtracted{env});
  DREAMOR_CHECK(isExactlyZero(plan(cI{1})) && exactlyEqual(plan(cI{2}), 2.0) && exactlyEqual(plan(cI{3}), 3.0));
  DREAMOR_CHECK_THROWS(IloException, x.solution(Refusing{env}));
}

void paramFromRecords() {
  const std::vector<ArcRecord> arcs{{1, 10, 2.5, "the-first-arc-label"}, {2, 20, 4.0, "the-second-arc-label"}};
  const auto arc_of   = [](const ArcRecord& a) { return std::tuple<cI, cJ>{a.from, a.to}; };
  const auto cost_of  = [](const ArcRecord& a) { return a.cost; };
  const auto label_of = [](const ArcRecord& a) -> const std::string& { return a.label; };
  const dreamor::ParamTable<double, cI, cJ> cost{arcs, arc_of, cost_of};
  const dreamor::ParamTable<std::string_view, cI, cJ> label{arcs, arc_of, label_of};
  DREAMOR_CHECK(cost.size() == 2 && exactlyEqual(cost(cI{2}, cJ{20}), 4.0) && exactlyEqual(cost(cI{1}, cJ{10}), 2.5));
  DREAMOR_CHECK(std::ranges::equal(cost.keys(), dreamor::IndexSet<cI, cJ>{arcs, arc_of}));
  DREAMOR_CHECK(label(cI{1}, cJ{10}).data() == arcs[0].label.data());
  const dreamor::ParamTable<double, cI, cJ> once{std::vector<ArcRecord>{{3, 30, 1.5, "x"}}, arc_of, cost_of};

  // a struct of the row's values, read at once: several values over one set, from one pass over the data
  struct Arc {
    double cost;
    std::string_view label;
  };

  const auto arc_values = [](const ArcRecord& a) { return Arc{a.cost, a.label}; };
  const dreamor::ParamTable<Arc, cI, cJ> arcValues{arcs, arc_of, arc_values};
  DREAMOR_CHECK(exactlyEqual(arcValues(cI{2}, cJ{20}).cost, 4.0) && arcValues(cI{1}, cJ{10}).label == arcs[0].label);
  const auto cost_from_arc = [&arcValues](cI i, cJ j) {
    return arcValues(i, j).cost;
  };  // a member, as a column callable
  DREAMOR_CHECK(exactlyEqual(dreamor::ParamTable<double, cI, cJ>(cost.keys(), cost_from_arc)(cI{1}, cJ{10}), 2.5));
  // a std::string value holds its text: made in the call, from records gone after the statement
  const auto tagged_of = [](const ArcRecord& a) { return a.label + "-tagged"; };
  const dreamor::ParamTable<std::string, cI, cJ> tagged{std::vector<ArcRecord>(arcs), arc_of, tagged_of};
  DREAMOR_CHECK(tagged(cI{2}, cJ{20}) == "the-second-arc-label-tagged");
  DREAMOR_CHECK(once.size() == 1 && exactlyEqual(once(cI{3}, cJ{30}), 1.5));
}

void valuesInKeyPlaces(const IloEnv& env) {
  const std::vector<std::uint32_t> periods{1, 2, 3};
  const std::vector<std::size_t> counts{10, 20};
  const auto sCP = dreamor::index_set::relational::from<cCount>(counts).cross<cPeriod>(periods).build();
  const dreamor::concert::VarTable<cCount, cPeriod> x{env, sCP, 0, 1, ILOFLOAT, "x"};
  const auto* const at_10_2 = x(cCount{10}, cPeriod{2}).getImpl();
  DREAMOR_CHECK(x(10, 2).getImpl() == at_10_2 && x(std::size_t{10}, std::uint32_t{2}).getImpl() == at_10_2);
  DREAMOR_CHECK(x(cCount{10}, 2).getImpl() == at_10_2 && x(10, cPeriod{2}).getImpl() == at_10_2);
  DREAMOR_CHECK(x(cCount{10}, cStart{2}).getImpl() == at_10_2);  // the same type under another name
  const auto sPERIODS         = dreamor::index_set::relational::from<cPeriod>(periods).build();
  const auto orderedP         = sPERIODS.ordered();
  const auto* const prev_of_3 = orderedP.prev(3);
  const std::uint64_t far     = std::uint64_t{1} << 33U;
  DREAMOR_CHECK(prev_of_3 != nullptr && x(10, *prev_of_3).getImpl() == at_10_2);  // a row of one column
  DREAMOR_CHECK(x(cCount{20}, orderedP.first()).getImpl() == x(20, 1).getImpl());
  DREAMOR_CHECK(x.contains(10, 2) && x.contains(20U, 3) && !x.contains(10, 4) && !x.contains(30, 1));
  DREAMOR_CHECK(!x.contains(10, -1) && !x.contains(-10, 2) && !x.contains(10, far));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(10, -1));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(10, far));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(10, 4));

  const dreamor::ParamTable<double, cCount, cPeriod> hours{sCP, 8.0};
  DREAMOR_CHECK(exactlyEqual(hours(20, 3), 8.0) && hours.contains(20, 1) && !hours.contains(20, -1));
  DREAMOR_CHECK_THROWS(std::out_of_range, hours(20, -1));
  const auto plan = x.solution(FakeAlgorithm{env});
  DREAMOR_CHECK(exactlyEqual(plan(10, 2), 1.0) && plan.contains(cCount{10}, cStart{3}) && !plan.contains(10, far));
  DREAMOR_CHECK_THROWS(std::out_of_range, plan(10, -2));

  // a Ref in its entity column's place, where no other key column holds that entity
  dreamor::EntityVector<named::Thing> things;
  static_cast<void>(things.emplace(named::Thing{"a"}));
  static_cast<void>(things.emplace(named::Thing{"b"}));
  things.freeze();
  const auto sFP = dreamor::index_set::relational::from<cFrom>(things).cross<cPeriod>(periods).build();
  const dreamor::concert::VarTable<cFrom, cPeriod> y{env, sFP, 0, 1, ILOFLOAT, "y"};
  DREAMOR_CHECK(y(things.at("b"), 3).getImpl() == y(cFrom{things.at("b")}, cPeriod{3}).getImpl());
  DREAMOR_CHECK(y(cTo{things.at("a")}, 1).getImpl() == y(cFrom{things.at("a")}, cPeriod{1}).getImpl());

  // what fills a key's place, by type; two columns of one type take only themselves
  using dreamor::internal::isKey;
  static_assert(isKey<std::tuple<cCount, cPeriod>, int, unsigned char>());
  static_assert(isKey<std::tuple<cCount, cPeriod>, cCount, std::tuple<cStart>>());
  static_assert(!isKey<std::tuple<cCount, cPeriod>, int>() && !isKey<std::tuple<cCount, cPeriod>, int, double>());
  static_assert(!isKey<std::tuple<cPeriod, cStart>, int, int>() &&
                !isKey<std::tuple<cPeriod, cStart>, cStart, cPeriod>());
  static_assert(isKey<std::tuple<cPeriod, cStart>, cPeriod, cStart>());
  static_assert(!isKey<std::tuple<cFrom, cTo>, dreamor::Ref<named::Thing>, cTo>());
  static_assert(!isKey<std::tuple<cCount, cPeriod>, cCount, bool>() &&
                !isKey<std::tuple<cCount, cPeriod>, char, int>());
}

// A table over one entity column whose rows are an EntityVector's entities, all of them in its order, finds an
// entity's row from the entity's address (RowIndex); over the same entities in another order, through the hash. A Ref
// to an entity of another vector is a row of neither.
void entityRowsByAddress(const IloEnv& env) {
  dreamor::EntityVector<named::Thing> things;
  for (const char* name : {"a", "b", "c", "d"}) {
    static_cast<void>(things.emplace(named::Thing{name}));
  }
  things.freeze();
  dreamor::EntityVector<named::Thing> others;
  static_cast<void>(others.emplace(named::Thing{"a"}));
  others.freeze();
  const auto in_order = dreamor::index_set::relational::from<cFrom>(things).build();
  const auto reversed = in_order.orderBy([](cFrom f) { return f->name; }, dreamor::descending);
  const dreamor::concert::VarTable<cFrom> by_address{env, in_order, 0, 1, ILOFLOAT, "a"};
  const dreamor::concert::VarTable<cFrom> by_hash{env, reversed, 0, 1, ILOFLOAT, "h"};
  const cFrom stranger{others.at("a")};
  const auto finds_each = [&stranger](const auto& table) {
    bool found = table.size() == 4;
    for (const auto& [row, var] : table) {
      const cFrom key{std::get<cFrom>(row).get()};  // a copy: found by the key, not by the row's address
      found = found && table.contains(key) && table(key).getImpl() == var.getImpl();
    }
    return found && !table.contains(stranger);
  };
  DREAMOR_CHECK(finds_each(by_address) && finds_each(by_hash));
  DREAMOR_CHECK_THROWS(std::out_of_range, by_address(stranger));
}

// A table moved from is empty, as a moved-from set is - a ParamTable, a SolutionTable and a VarTable alike: every
// member works on it and finds nothing, a copy of it is empty too, and a table assigned over it is whole again.
void movedFrom(const IloEnv& env) {
  const std::vector<std::size_t> I{1, 2, 3};
  const auto sI = dreamor::index_set::relational::from<cI>(I).build();
  dreamor::ParamTable<double, cI> cost{sI, 2.5};
  const auto keptCost = std::move(cost);
  dreamor::concert::VarTable<cI> x{env, sI, 0, 1, ILOFLOAT, "x"};
  auto plan           = x.solution(FakeAlgorithm{env});
  const auto keptPlan = std::move(plan);
  const dreamor::concert::VarTable<cI> keptX{std::move(x)};
  // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move): reading what a move leaves is the test
  DREAMOR_CHECK(keptCost.size() == 3 && cost.size() == 0 && cost.begin() == cost.end() && !cost.contains(cI{2}));
  DREAMOR_CHECK_THROWS(std::out_of_range, cost(cI{2}));
  const auto copied = cost;
  DREAMOR_CHECK(copied.size() == 0 && !copied.contains(cI{2}));
  cost = keptCost;
  DREAMOR_CHECK(cost.contains(cI{2}) && exactlyEqual(cost(cI{2}), 2.5));
  DREAMOR_CHECK(keptPlan.size() == 3 && plan.size() == 0 && plan.begin() == plan.end() && !plan.contains(cI{2}));
  DREAMOR_CHECK_THROWS(std::out_of_range, plan(cI{2}));
  plan = keptPlan;
  DREAMOR_CHECK(plan.contains(cI{2}));
  DREAMOR_CHECK(keptX.size() == 3 && keptX.contains(cI{2}));
  DREAMOR_CHECK(x.size() == 0 && x.keys().empty() && x.begin() == x.end() && !x.contains(cI{2}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(cI{2}));
  DREAMOR_CHECK(x.solution(FakeAlgorithm{env}).size() == 0);
  // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

// ParamTable{set, value} deduces the type from the value, as std::tuple does: a number its type (0 is int), a string
// literal std::string_view, a pointer to char or a char buffer std::string (a copy: their text's life is unknown), a
// callable what it returns without const or a reference, another table its value type.
void deducedParamTypes() {
  const std::vector<std::size_t> I{1, 2, 3};
  const auto sI                   = dreamor::index_set::relational::from<cI>(I).build();
  static const std::string kLabel = "label";
  const auto label_of             = [](cI /*i*/) -> const std::string& { return kLabel; };
  const auto half_of              = [](cI i) { return static_cast<float>(i.get()) / 2; };
  const dreamor::ParamTable count{sI, 0};
  const dreamor::ParamTable hours{sI, 2.5};
  const dreamor::ParamTable name{sI, "none"};
  const dreamor::ParamTable label{sI, label_of};
  const dreamor::ParamTable half{sI, half_of};
  const dreamor::ParamTable again{sI, half};
  static_assert(std::is_same_v<decltype(count), const dreamor::ParamTable<int, cI>>);
  static_assert(std::is_same_v<decltype(hours), const dreamor::ParamTable<double, cI>>);
  static_assert(std::is_same_v<decltype(name), const dreamor::ParamTable<std::string_view, cI>>);
  static_assert(std::is_same_v<decltype(label), const dreamor::ParamTable<std::string, cI>>);
  static_assert(std::is_same_v<decltype(half), const dreamor::ParamTable<float, cI>>);
  static_assert(std::is_same_v<decltype(again), const dreamor::ParamTable<float, cI>>);
  // Text whose life the table cannot know is copied: a pointer to char, and a buffer that is not const.
  const char* const pointed = kLabel.c_str();
  char buffer[8]            = "buffer";  // NOLINT(*-avoid-c-arrays): the buffer of a function is the case
  const dreamor::ParamTable fromPointer{sI, pointed};
  const dreamor::ParamTable fromBuffer{sI, buffer};
  static_assert(std::is_same_v<decltype(fromPointer), const dreamor::ParamTable<std::string, cI>>);
  static_assert(std::is_same_v<decltype(fromBuffer), const dreamor::ParamTable<std::string, cI>>);
  const auto code_of = [](cI /*i*/) -> const char* { return "code"; };  // text of unknown life, from a callable
  const dreamor::ParamTable codes{sI, code_of};
  static_assert(std::is_same_v<decltype(codes), const dreamor::ParamTable<std::string, cI>>);
  DREAMOR_CHECK(codes(cI{2}) == "code");
  buffer[0] = 'B';  // the table holds its own copy
  DREAMOR_CHECK(fromPointer(cI{1}) == "label" && fromBuffer(cI{2}) == "buffer");
  DREAMOR_CHECK(count(cI{1}) == 0 && exactlyEqual(hours(cI{2}), 2.5) && name(cI{3}) == "none");
  DREAMOR_CHECK(label(cI{1}) == "label" && exactlyEqual(static_cast<double>(again(cI{3})), 1.5));
}

// A table constructed with no arguments is empty, as one moved from; a family moved into an empty VarTable, once, is
// whole there - the table_filled_once contract - and so is a ParamTable or a SolutionTable assigned to an empty one.
void constructedEmpty(const IloEnv& env) {
  const std::vector<std::size_t> I{1, 2, 3};
  const auto sI = dreamor::index_set::relational::from<cI>(I).build();
  static_assert(std::is_nothrow_default_constructible_v<dreamor::ParamTable<double, cI>>);
  static_assert(std::is_nothrow_default_constructible_v<dreamor::SolutionTable<cI>>);
  dreamor::ParamTable<double, cI> cost;
  dreamor::SolutionTable<cI> plan;
  dreamor::concert::VarTable<cI> x;
  DREAMOR_CHECK(cost.size() == 0 && !cost.contains(cI{1}) && plan.size() == 0 && !plan.contains(cI{1}));
  DREAMOR_CHECK(x.size() == 0 && x.keys().empty() && x.begin() == x.end() && !x.contains(cI{1}));
  DREAMOR_CHECK_THROWS(std::out_of_range, cost(cI{1}));
  x = dreamor::concert::VarTable<cI>{env, sI, 0, 1, ILOFLOAT, "x"};
  DREAMOR_CHECK(x.size() == 3 && x.contains(cI{2}) && std::string{x(cI{2}).getName()} == "x_2");
  cost = dreamor::ParamTable<double, cI>{sI, 2.5};
  plan = x.solution(FakeAlgorithm{env});
  DREAMOR_CHECK(exactlyEqual(cost(cI{3}), 2.5) && plan.size() == 3 && plan.contains(cI{3}));
  // A table moved from takes a family again.
  const dreamor::concert::VarTable<cI> y{std::move(x)};
  x = dreamor::concert::VarTable<cI>{env, sI, 0, 1, ILOFLOAT, "z"};
  DREAMOR_CHECK(y.size() == 3 && x.size() == 3 && std::string{x(cI{1}).getName()} == "z_1");
}

}  // namespace

int main() try {
  const IloEnv env;
  const std::vector<std::size_t> I{3, 1, 2};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).build();

  static_assert(!std::is_copy_constructible_v<dreamor::concert::VarTable<cI, cJ>>);
  static_assert(std::is_nothrow_move_constructible_v<dreamor::concert::VarTable<cI, cJ>>);
  static_assert(std::is_nothrow_default_constructible_v<dreamor::concert::VarTable<cI, cJ>> &&
                std::is_nothrow_move_assignable_v<dreamor::concert::VarTable<cI, cJ>>);

  // numbers
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ, 0, 1, ILOINT, "x"};
  DREAMOR_CHECK(x.size() == 6 && x.keys().begin() == IJ.begin());
  DREAMOR_CHECK(x(cI{1}, cJ{20}).getImpl() != nullptr && x.contains(cI{1}, cJ{20}));
  DREAMOR_CHECK(!x.contains(cI{9}, cJ{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(cI{9}, cJ{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(std::tuple{cI{9}, cJ{9}}));
  DREAMOR_CHECK(std::string{x(cI{1}, cJ{20}).getName()} == "x_1_20");
  DREAMOR_CHECK(exactlyEqual(x(cI{3}, cJ{10}).getLB(), 0) && exactlyEqual(x(cI{3}, cJ{10}).getUB(), 1));

  // iteration: (row, var) in set order; the iterator steps forward either way
  auto step          = x.begin();
  const auto atFirst = step++;
  DREAMOR_CHECK(atFirst == x.begin() && step != atFirst && (*step).first == *(IJ.begin() + 1));
  std::size_t n = 0;
  for (const auto& [row, var] : x) {
    DREAMOR_CHECK(row == *(IJ.begin() + static_cast<std::ptrdiff_t>(n)));
    DREAMOR_CHECK(var.getImpl() == x(row).getImpl());
    ++n;
  }
  DREAMOR_CHECK(n == 6);

  // a row of the table's own set - walked directly or through a group - is found by its
  // address; an equal row anywhere else, by hashing; both give the same variable
  const auto& firstRow            = *IJ.begin();
  const std::tuple<cI, cJ> copied = firstRow;
  const auto sameValues           = IJ.where([](cI /*i*/) { return true; });
  DREAMOR_CHECK(x(firstRow).getImpl() == x(copied).getImpl() &&
                x(copied).getImpl() == x(*sameValues.begin()).getImpl());
  const auto byI     = IJ.groupBy<cI>();
  bool groupsAgree   = true;
  std::size_t inRows = 0;
  for (const auto& [i, rows] : byI) {
    for (const auto& row : rows) {
      groupsAgree = groupsAgree && x(row).getImpl() == x(i, std::get<cJ>(row)).getImpl() && x.contains(row);
      ++inRows;
    }
  }
  DREAMOR_CHECK(groupsAgree && inRows == 6);

  // outside a sum, a lookup by columns tries the last row found - and, after a step to the next
  // row, the row after it - before hashing; a hint, never trusted: any three lookups in a row, in
  // this table, in a table on another set or of an absent key, give what the row's address gives
  const std::vector<std::size_t> I2{2, 1};
  const auto IJ2 = dreamor::index_set::relational::from<cI>(I2).cross<cJ>(J).build();
  const dreamor::concert::VarTable<cI, cJ> x2{env, IJ2, 0, 1};
  std::vector<std::tuple<cI, cJ>> keys(IJ.begin(), IJ.end());
  keys.emplace_back(cI{9}, cJ{9});
  bool hintsAgree = true;
  for (const auto& p : keys) {
    for (const auto& q : keys) {
      for (const auto& r : keys) {
        for (const auto& key : {p, q, r}) {
          const auto [i, j] = key;
          const auto at     = std::ranges::find(IJ, key);
          hintsAgree        = hintsAgree && x.contains(i, j) == (at != IJ.end());
          if (at != IJ.end()) { hintsAgree = hintsAgree && x(i, j).getImpl() == x(*at).getImpl(); }
          if (x2.contains(i, j)) { hintsAgree = hintsAgree && x2(i, j).getImpl() == x2(key).getImpl(); }
        }
      }
    }
  }
  DREAMOR_CHECK(hintsAgree);

  // more sets in turn than there are hints, and hints left by sets gone where new ones now are:
  // each lookup gives what the row's address gives
  bool turnsAgree = true;
  for (std::size_t round = 0; round < 3; ++round) {
    std::vector<dreamor::IndexSet<cI, cJ>> sets;
    sets.reserve(6);
    for (std::size_t s = 0; s < 6; ++s) {
      const std::vector<std::size_t> Is{s + round, s + round + 1};
      sets.push_back(dreamor::index_set::relational::from<cI>(Is).cross<cJ>(J).build());
    }
    std::vector<dreamor::concert::VarTable<cI, cJ>> tables;
    tables.reserve(sets.size());
    for (const auto& set : sets) {
      tables.emplace_back(env, set, 0, 1);
    }
    for (const std::size_t turns : {2U, 5U, 6U}) {
      for (std::size_t at = 0; at < sets.front().size(); ++at) {
        for (std::size_t s = 0; s < turns; ++s) {
          const auto& row   = *std::next(sets[s].begin(), static_cast<std::ptrdiff_t>(at));
          const auto [i, j] = row;
          turnsAgree        = turnsAgree && tables[s](i, j).getImpl() == tables[s](row).getImpl();
        }
      }
    }
  }
  DREAMOR_CHECK(turnsAgree);

  // a hint inside a row, not at its start - what a row of a set gone, of another key, leaves where
  // a set now is - on rows of 24 bytes, whose size is no power of two: every lookup still gives what
  // the row's address gives. Small values, so that the bytes across two rows read as a row's columns.
  const std::vector<std::size_t> V{0, 1, 2};
  const auto IJK = dreamor::index_set::relational::from<cI>(V).cross<cJ>(V).cross<cK>(V).build();
  const dreamor::concert::VarTable<cI, cJ, cK> w{env, IJK, 0, 1};
  bool insideAgrees = true;
  for (const auto& hinted : IJK) {
    for (const std::size_t offset : {sizeof(std::size_t), 2 * sizeof(std::size_t)}) {
      for (const auto& row : IJK) {
        const auto* const inside = std::bit_cast<const void*>(std::bit_cast<std::uintptr_t>(&hinted) + offset);
        dreamor::internal::lastRows().front() = {.row = inside, .walking = true};
        const auto [i, j, k]                  = row;
        insideAgrees                          = insideAgrees && w(i, j, k).getImpl() == w(row).getImpl();
      }
    }
  }
  DREAMOR_CHECK(insideAgrees);

  // callable bounds: one column, two columns
  const dreamor::concert::VarTable<cI, cJ> y{env,
                                             IJ,
                                             [](cI i) { return -static_cast<double>(i.get()); },
                                             [](cI i, cJ j) { return i.get() * j.get(); },
                                             ILOFLOAT,
                                             "y"};
  DREAMOR_CHECK(exactlyEqual(y(cI{2}, cJ{20}).getLB(), -2) && exactlyEqual(y(cI{2}, cJ{20}).getUB(), 40));

  // defaults, unnamed
  const dreamor::concert::VarTable<cI, cJ> z{env, IJ};
  DREAMOR_CHECK(exactlyEqual(z(cI{3}, cJ{20}).getLB(), 0) && exactlyEqual(z(cI{3}, cJ{20}).getUB(), IloInfinity));
  DREAMOR_CHECK(std::strlen(z(cI{3}, cJ{20}).getName()) == 0);

  // a name of any text - a std::string, std::format's - and a lookup outside the set names its key
  const std::string family = std::format("w{}", 2);
  const dreamor::concert::VarTable<cI, cJ> wide2{env, IJ, 0, 1, ILOFLOAT, family};
  DREAMOR_CHECK(std::string{wide2(cI{1}, cJ{20}).getName()} == "w2_1_20");
  std::string missing;
  try {
    (void)wide2(cI{9}, cJ{9});
  } catch (const std::out_of_range& error) { missing = error.what(); }
  DREAMOR_CHECK(missing == "VarTable: no row (9, 9) in the table's set.");

  // semi-continuous
  const dreamor::concert::VarTable<cI, cJ> sc{dreamor::concert::semiContinuous, env, IJ, 5, 50, ILOFLOAT, "sc"};
  DREAMOR_CHECK(sc(cI{1}, cJ{10}).getImpl()->semicont && exactlyEqual(sc(cI{1}, cJ{10}).getLB(), 5));

  // bounds tightened afterwards: the variables are ordinary Concert objects
  const auto atTwo = [](cI i) { return i.get() == 2; };
  for (const auto& row : x.keys().where(atTwo)) {
    x(row).setUB(0);
  }
  DREAMOR_CHECK(exactlyEqual(x(cI{2}, cJ{10}).getUB(), 0) && exactlyEqual(x(cI{3}, cJ{10}).getUB(), 1));

  // tables on one set share its row index, built by the first: one built after every other
  // table on the set is gone, a value table that outlives its table, and tables built on several
  // threads at once - one env each, as Concert requires - all find every row
  const std::vector<std::size_t> I3{7, 8, 9};
  const auto IJ3           = dreamor::index_set::relational::from<cI>(I3).cross<cJ>(J).build();
  const auto findsEveryRow = [&IJ3](const dreamor::concert::VarTable<cI, cJ>& t) {
    bool all = t.size() == IJ3.size();
    for (const auto& [row, var] : t) {
      all = all && t(std::get<cI>(row), std::get<cJ>(row)).getImpl() == var.getImpl();
    }
    return all && !t.contains(cI{1}, cJ{10});
  };
  {
    const dreamor::concert::VarTable<cI, cJ> first{env, IJ3, 0, 1};
    DREAMOR_CHECK(findsEveryRow(first));
  }
  const auto values = [&] {
    const dreamor::concert::VarTable<cI, cJ> again{env, IJ3, 0, 1};
    DREAMOR_CHECK(findsEveryRow(again));
    return again.solution(FakeAlgorithm{env});
  }();
  DREAMOR_CHECK(exactlyEqual(values(cI{8}, cJ{20}), 1) && !values.contains(cI{1}, cJ{10}));
  std::vector<std::optional<dreamor::concert::VarTable<cI, cJ>>> onThreads(8);
  {
    std::vector<std::thread> threads;
    threads.reserve(onThreads.size());
    for (auto& slot : onThreads) {
      threads.emplace_back([&slot, &IJ3] { slot.emplace(IloEnv{}, IJ3, 0, 1); });
    }
    for (std::thread& thread : threads) {
      thread.join();
    }
  }
  bool threadsAgree = true;
  for (const auto& t : onThreads) {
    threadsAgree = threadsAgree && t.has_value() && findsEveryRow(*t);
  }
  DREAMOR_CHECK(threadsAgree);

  // solution: one getValues on the variables gathered once, values keyed like the table, the values' array ended
  const std::size_t arraysBefore = IloNumVarArray::ended;
  const std::size_t numsBefore   = IloNumArray::ended;
  const auto v                   = y.solution(FakeAlgorithm{env});
  DREAMOR_CHECK(IloNumVarArray::ended == arraysBefore && IloNumArray::ended == numsBefore + 1);
  const auto again = y.solution(FakeAlgorithm{env});  // on the variables gathered by the first call
  DREAMOR_CHECK(again.size() == 6 && exactlyEqual(again(cI{2}, cJ{20}), v(cI{2}, cJ{20})));
  DREAMOR_CHECK(v.size() == 6 && exactlyEqual(v(cI{2}, cJ{20}), -2 + 40) && v.contains(cI{1}, cJ{10}));
  DREAMOR_CHECK_THROWS(std::out_of_range, v(cI{9}, cJ{9}));
  std::size_t m = 0;
  for (const auto& [row, value] : v) {
    DREAMOR_CHECK(exactlyEqual(value, y(row).getLB() + y(row).getUB()));
    ++m;
  }
  DREAMOR_CHECK(m == 6);
  const auto& lastRow = *(IJ.begin() + 5);
  DREAMOR_CHECK(exactlyEqual(v(lastRow), v(std::get<cI>(lastRow), std::get<cJ>(lastRow))) && v.contains(lastRow));

  // a solve's values are a SolutionTable: only solution() makes one
  static_assert(std::is_same_v<std::remove_const_t<decltype(v)>, dreamor::SolutionTable<cI, cJ>>);
  static_assert(!std::is_constructible_v<dreamor::SolutionTable<cI, cJ>, decltype(IJ), double>);

  // a ParamTable of data on a set (param {IJ}): one value for every row, or a callable over the row's columns;
  // it shares the set's row index with the tables on it, and reads like a solution
  const dreamor::ParamTable<double, cI, cJ> ones{IJ, 1};
  const auto i_plus_j = [](cI i, cJ j) { return static_cast<double>(i.get() + j.get()); };
  const dreamor::ParamTable<double, cI, cJ> sums{IJ, i_plus_j};
  DREAMOR_CHECK(ones.size() == 6 && exactlyEqual(ones(cI{2}, cJ{20}), 1.0));
  DREAMOR_CHECK(sums.size() == 6 && exactlyEqual(sums(cI{1}, cJ{10}), 11.0) && exactlyEqual(sums(lastRow), 22.0));
  DREAMOR_CHECK_THROWS(std::out_of_range, sums(cI{9}, cJ{9}));
  bool inSetOrder = true;
  auto row        = IJ.begin();
  for (const auto& [key, value] : sums) {
    inSetOrder = inSetOrder && key == *row++ && exactlyEqual(value, i_plus_j(std::get<cI>(key), std::get<cJ>(key)));
  }
  DREAMOR_CHECK(inSetOrder && row == IJ.end());
  const auto ten_i = [](cI i) { return 10.0 * static_cast<double>(i.get()); };  // a callable may read some columns
  DREAMOR_CHECK(exactlyEqual(dreamor::ParamTable<double, cI, cJ>(IJ, ten_i)(cI{3}, cJ{10}), 30.0));
  DREAMOR_CHECK(dreamor::ParamTable<double, cI, cJ>(IJ.where([](cI i) { return i.get() > 100; }), 1.0).size() == 0);
  // a ParamTable is a column callable: a VarTable's bound, and the value of another ParamTable on fewer or more
  // columns
  const dreamor::concert::VarTable<cI, cJ> capped{env, IJ, 0, sums};
  DREAMOR_CHECK(exactlyEqual(capped(cI{2}, cJ{20}).getUB(), 22.0) && exactlyEqual(capped(cI{3}, cJ{10}).getUB(), 13.0));
  const auto II                                  = dreamor::index_set::relational::from<cI>(I).build();
  const dreamor::ParamTable<double, cI> tens     = {II, ten_i};
  const dreamor::ParamTable<double, cI, cJ> wide = {IJ, tens};  // reads the cI of each row
  DREAMOR_CHECK(exactlyEqual(wide(cI{2}, cJ{20}), 20.0) && exactlyEqual(wide(cI{3}, cJ{10}), 30.0));
  // a solve's values carried into the next model: a ParamTable built from the SolutionTable
  const dreamor::ParamTable<double, cI, cJ> carried{IJ, v};
  DREAMOR_CHECK(exactlyEqual(carried(cI{2}, cJ{20}), v(cI{2}, cJ{20})) && carried.size() == v.size());
  // values of other types: an integer, a flag, a text
  const auto parity_of = [](cI i) { return i.get() % 2 == 0; };
  const auto label_of  = [](cJ j) { return j.get() == 10 ? std::string_view{"ten"} : std::string_view{"more"}; };
  const dreamor::ParamTable<std::uint32_t, cI, cJ> sevens{IJ, std::uint32_t{7}};
  const dreamor::ParamTable<bool, cI, cJ> even{IJ, parity_of};
  const dreamor::ParamTable<std::string_view, cI, cJ> labels{IJ, label_of};
  DREAMOR_CHECK(sevens(cI{1}, cJ{10}) == 7 && even(cI{2}, cJ{20}) && !even(cI{1}, cJ{20}));
  DREAMOR_CHECK(labels(cI{1}, cJ{10}) == "ten" && labels(cI{3}, cJ{20}) == "more");
  // a text found in strings that outlive the table, returned by reference: the table shows those strings
  const std::vector<std::string> names{"label-for-ten-long-enough", "label-for-more-long-enough"};
  const auto name_of = [&names](cJ j) -> const std::string& { return names[j.get() == 10 ? 0 : 1]; };
  const dreamor::ParamTable<std::string_view, cI, cJ> named{IJ, name_of};
  DREAMOR_CHECK(named(cI{1}, cJ{10}).data() == names[0].data() && named(cI{3}, cJ{20}) == names[1]);
  std::size_t evenRows = 0;
  for (const auto& [key, flag] : even) {
    if (flag && std::get<cI>(key).get() % 2 == 0) { ++evenRows; }
  }
  DREAMOR_CHECK(evenRows == 2);
  const dreamor::ParamTable<std::string_view, cI, cJ> unnamed{IJ, "none"};
  DREAMOR_CHECK(unnamed(lastRow) == "none");

  // empty set: nothing to read, no solver call needed
  const auto none = IJ.where([](cI i) { return i.get() > 100; });
  const dreamor::concert::VarTable<cI, cJ> e{env, none, 0, 1};
  DREAMOR_CHECK(e.size() == 0 && e.solution(FakeAlgorithm{env}).size() == 0 && e.begin() == e.end());

  // move keeps lookups working
  dreamor::concert::VarTable<cI, cJ> moved{env, IJ, 0, 1, ILOINT, "w"};
  const auto* const before = moved(cI{1}, cJ{10}).getImpl();
  const dreamor::concert::VarTable<cI, cJ> moved2{std::move(moved)};
  DREAMOR_CHECK(moved2(cI{1}, cJ{10}).getImpl() == before);

  // a name token whose formatter throws: the name stops after the separator (and a warning)
  dreamor::EntityVector<throwing::Holder> things;
  static_cast<void>(things.emplace(throwing::Holder{throwing::Token{1}}));
  things.freeze();
  const auto sTHINGS = dreamor::index_set::relational::from<cThing>(things).build();
  const dreamor::concert::VarTable<cThing> t{env, sTHINGS, 0, 1, ILOFLOAT, "t"};
  DREAMOR_CHECK(std::string{t(cThing{things.at(throwing::Token{1})}).getName()} == "t_");

  // text keys - a primaryKey() returning a std::string - written as they are, a name too long cut
  // at the buffer (and a warning)
  dreamor::EntityVector<named::Thing> named_things;
  static_cast<void>(named_things.emplace(named::Thing{"M3"}));
  static_cast<void>(named_things.emplace(named::Thing{std::string(300, 'z')}));
  named_things.freeze();
  const std::vector<std::size_t> seven{7};
  const auto sNAMED = dreamor::index_set::relational::from<cNamed>(named_things).build();
  const dreamor::concert::VarTable<cNamed, cJ> vNamed{
      env, dreamor::index_set::relational::from(sNAMED).cross<cJ>(seven).build(), 0, 1, ILOFLOAT, "n"};
  const cNamed m3{named_things.at("M3")};
  DREAMOR_CHECK(std::string{vNamed(m3, cJ{7}).getName()} == "n_M3_7");
  // a column under std::format prints the token its variable's name has
  DREAMOR_CHECK(std::format("n_{}_{}", m3, cJ{7}) == vNamed(m3, cJ{7}).getName());
  const std::string cut{vNamed(cNamed{named_things.at(std::string(300, 'z'))}, cJ{7}).getName()};
  DREAMOR_CHECK(cut.size() == 255 && cut == "n_" + std::string(253, 'z'));
  valuesInKeyPlaces(env);
  entityRowsByAddress(env);
  unextractedVariables(env);
  paramFromRecords();
  movedFrom(env);
  constructedEmpty(env);
  deducedParamTypes();
  return report("vartable");
} catch (...) { return ::uncaught(); }
