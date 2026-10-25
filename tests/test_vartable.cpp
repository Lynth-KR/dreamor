// SPDX-License-Identifier: Apache-2.0
// VarTable: built once from a set, bounds as numbers or column callables, names from keys,
// lookup, iteration in set order, solution().
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"
using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;

// An entity whose name token's formatter throws: the variable name stops after the separator,
// with a warning, instead of the throw ending the noexcept naming in std::terminate.
namespace {

namespace throwing {
struct Token {};
struct Thing {};
inline Token keyName(const Thing* /*thing*/) {
  return {};
}
}  // namespace throwing
namespace named {
struct Thing {
  std::string name;
};
inline const std::string& keyName(const Thing* thing) {
  return thing->name;
}
}  // namespace named
}  // namespace

template <>
struct std::formatter<throwing::Token> {
  static constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
  static auto format(const throwing::Token& /*token*/, std::format_context& ctx) -> decltype(ctx.out()) {
    throw std::runtime_error("this token cannot be formatted");
  }
};
using ThingCol = dreamor::Col<const throwing::Thing*, "thing">;
using NamedCol = dreamor::Col<const named::Thing*, "named">;

namespace {

// Stands in for IloCplex: getEnv() and getValues(IloNumArray, IloNumVarArray).
struct FakeAlgorithm {
  IloEnv env;
  [[nodiscard]] IloEnv getEnv() const { return env; }
  // "Solution" = lb + ub, so each value is checkable from the variable itself.
  static void getValues(IloNumArray out, const IloNumVarArray vars) {
    for (IloInt i = 0; i < vars.getSize(); ++i) out.add(vars[i].getLB() + vars[i].getUB());
  }
};

}  // namespace

int main() try {
  const IloEnv env;
  const std::vector<std::size_t> I{3, 1, 2};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::makeSet<ICol, JCol>(I, J).collect();

  static_assert(!std::is_copy_constructible_v<dreamor::concert::VarTable<ICol, JCol>>);
  static_assert(std::is_nothrow_move_constructible_v<dreamor::concert::VarTable<ICol, JCol>>);
  static_assert(!std::is_default_constructible_v<dreamor::concert::VarTable<ICol, JCol>>);

  // numbers
  const dreamor::concert::VarTable<ICol, JCol> x{env, IJ, 0, 1, ILOINT, "x"};
  DREAMOR_CHECK(x.size() == 6 && x.keys().begin() == IJ.begin());
  DREAMOR_CHECK(x(ICol{1}, JCol{20}).getImpl() != nullptr && x.contains(ICol{1}, JCol{20}));
  DREAMOR_CHECK(!x.contains(ICol{9}, JCol{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(ICol{9}, JCol{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(std::tuple{ICol{9}, JCol{9}}));
  DREAMOR_CHECK(std::string{x(ICol{1}, JCol{20}).getName()} == "x_1_20");
  DREAMOR_CHECK(exactlyEqual(x(ICol{3}, JCol{10}).getLB(), 0) && exactlyEqual(x(ICol{3}, JCol{10}).getUB(), 1));

  // iteration: (row, var) in set order
  std::size_t n = 0;
  for (const auto& [row, var] : x) {
    DREAMOR_CHECK(row == *(IJ.begin() + static_cast<std::ptrdiff_t>(n)));
    DREAMOR_CHECK(var.getImpl() == x(row).getImpl());
    ++n;
  }
  DREAMOR_CHECK(n == 6);

  // a row of the table's own set - walked directly or through a group - is found by its
  // address; an equal row anywhere else, by hashing; both give the same variable
  const auto& firstRow                = *IJ.begin();
  const std::tuple<ICol, JCol> copied = firstRow;
  const auto sameValues               = IJ.where([](ICol /*i*/) { return true; });
  DREAMOR_CHECK(x(firstRow).getImpl() == x(copied).getImpl() &&
                x(copied).getImpl() == x(*sameValues.begin()).getImpl());
  const auto byI     = IJ.groupBy<ICol>();
  bool groupsAgree   = true;
  std::size_t inRows = 0;
  for (const auto& [i, rows] : byI) {
    for (const auto& row : rows) {
      groupsAgree = groupsAgree && x(row).getImpl() == x(i, std::get<JCol>(row)).getImpl() && x.contains(row);
      ++inRows;
    }
  }
  DREAMOR_CHECK(groupsAgree && inRows == 6);

  // outside a sum, a lookup by columns tries the last row found - and, after a step to the next
  // row, the row after it - before hashing; a hint, never trusted: any three lookups in a row, in
  // this table, in a table on another set or of an absent key, give what the row's address gives
  const std::vector<std::size_t> I2{2, 1};
  const auto IJ2 = dreamor::makeSet<ICol, JCol>(I2, J).collect();
  const dreamor::concert::VarTable<ICol, JCol> x2{env, IJ2, 0, 1};
  std::vector<std::tuple<ICol, JCol>> keys(IJ.begin(), IJ.end());
  keys.emplace_back(ICol{9}, JCol{9});
  bool hintsAgree = true;
  for (const auto& p : keys) {
    for (const auto& q : keys) {
      for (const auto& r : keys) {
        for (const auto& key : {p, q, r}) {
          const auto [i, j] = key;
          const auto at     = std::ranges::find(IJ, key);
          hintsAgree        = hintsAgree && x.contains(i, j) == (at != IJ.end());
          if (at != IJ.end()) hintsAgree = hintsAgree && x(i, j).getImpl() == x(*at).getImpl();
          if (x2.contains(i, j)) hintsAgree = hintsAgree && x2(i, j).getImpl() == x2(key).getImpl();
        }
      }
    }
  }
  DREAMOR_CHECK(hintsAgree);

  // callable bounds: one column, two columns
  const dreamor::concert::VarTable<ICol, JCol> y{env,
                                                 IJ,
                                                 [](ICol i) { return -static_cast<double>(i.get()); },
                                                 [](ICol i, JCol j) { return i.get() * j.get(); },
                                                 ILOFLOAT,
                                                 "y"};
  DREAMOR_CHECK(exactlyEqual(y(ICol{2}, JCol{20}).getLB(), -2) && exactlyEqual(y(ICol{2}, JCol{20}).getUB(), 40));

  // defaults, unnamed
  const dreamor::concert::VarTable<ICol, JCol> z{env, IJ};
  DREAMOR_CHECK(exactlyEqual(z(ICol{3}, JCol{20}).getLB(), 0) &&
                exactlyEqual(z(ICol{3}, JCol{20}).getUB(), IloInfinity));
  DREAMOR_CHECK(std::strlen(z(ICol{3}, JCol{20}).getName()) == 0);

  // semi-continuous
  const dreamor::concert::VarTable<ICol, JCol> sc{dreamor::concert::semiContinuous, env, IJ, 5, 50, ILOFLOAT, "sc"};
  DREAMOR_CHECK(sc(ICol{1}, JCol{10}).getImpl()->semicont && exactlyEqual(sc(ICol{1}, JCol{10}).getLB(), 5));

  // bounds tightened afterwards: the variables are ordinary Concert objects
  const auto atTwo = [](ICol i) { return i.get() == 2; };
  for (const auto& row : x.keys().where(atTwo)) x(row).setUB(0);
  DREAMOR_CHECK(exactlyEqual(x(ICol{2}, JCol{10}).getUB(), 0) && exactlyEqual(x(ICol{3}, JCol{10}).getUB(), 1));

  // tables on one set share its row index, built by the first: one built after every other
  // table on the set is gone, a value table that outlives its table, and tables built on several
  // threads at once - one env each, as Concert requires - all find every row
  const std::vector<std::size_t> I3{7, 8, 9};
  const auto IJ3           = dreamor::makeSet<ICol, JCol>(I3, J).collect();
  const auto findsEveryRow = [&IJ3](const dreamor::concert::VarTable<ICol, JCol>& t) {
    bool all = t.size() == IJ3.size();
    for (const auto& [row, var] : t)
      all = all && t(std::get<ICol>(row), std::get<JCol>(row)).getImpl() == var.getImpl();
    return all && !t.contains(ICol{1}, JCol{10});
  };
  {
    const dreamor::concert::VarTable<ICol, JCol> first{env, IJ3, 0, 1};
    DREAMOR_CHECK(findsEveryRow(first));
  }
  const auto values = [&] {
    const dreamor::concert::VarTable<ICol, JCol> again{env, IJ3, 0, 1};
    DREAMOR_CHECK(findsEveryRow(again));
    return again.solution(FakeAlgorithm{env});
  }();
  DREAMOR_CHECK(exactlyEqual(values(ICol{8}, JCol{20}), 1) && !values.contains(ICol{1}, JCol{10}));
  std::vector<std::optional<dreamor::concert::VarTable<ICol, JCol>>> onThreads(8);
  {
    std::vector<std::jthread> threads;
    threads.reserve(onThreads.size());
    for (auto& slot : onThreads) threads.emplace_back([&slot, &IJ3] { slot.emplace(IloEnv{}, IJ3, 0, 1); });
  }
  bool threadsAgree = true;
  for (const auto& t : onThreads) threadsAgree = threadsAgree && t.has_value() && findsEveryRow(*t);
  DREAMOR_CHECK(threadsAgree);

  // solution: one call, values keyed like the table, temporaries ended
  const std::size_t arraysBefore = IloNumVarArray::ended;
  const std::size_t numsBefore   = IloNumArray::ended;
  const auto v                   = y.solution(FakeAlgorithm{env});
  DREAMOR_CHECK(IloNumVarArray::ended == arraysBefore + 1 && IloNumArray::ended == numsBefore + 1);
  DREAMOR_CHECK(v.size() == 6 && exactlyEqual(v(ICol{2}, JCol{20}), -2 + 40) && v.contains(ICol{1}, JCol{10}));
  DREAMOR_CHECK_THROWS(std::out_of_range, v(ICol{9}, JCol{9}));
  std::size_t m = 0;
  for (const auto& [row, value] : v) {
    DREAMOR_CHECK(exactlyEqual(value, y(row).getLB() + y(row).getUB()));
    ++m;
  }
  DREAMOR_CHECK(m == 6);
  const auto& lastRow = *(IJ.begin() + 5);
  DREAMOR_CHECK(exactlyEqual(v(lastRow), v(std::get<ICol>(lastRow), std::get<JCol>(lastRow))) && v.contains(lastRow));

  // empty set: nothing to read, no solver call needed
  const auto none = IJ.where([](ICol i) { return i.get() > 100; });
  const dreamor::concert::VarTable<ICol, JCol> e{env, none, 0, 1};
  DREAMOR_CHECK(e.size() == 0 && e.solution(FakeAlgorithm{env}).size() == 0 && e.begin() == e.end());

  // move keeps lookups working
  dreamor::concert::VarTable<ICol, JCol> moved{env, IJ, 0, 1, ILOINT, "w"};
  const auto* const before = moved(ICol{1}, JCol{10}).getImpl();
  const dreamor::concert::VarTable<ICol, JCol> moved2{std::move(moved)};
  DREAMOR_CHECK(moved2(ICol{1}, JCol{10}).getImpl() == before);

  // a name token whose formatter throws: the name stops after the separator (and a warning)
  const std::vector<throwing::Thing> things(1);
  const auto sTHINGS = dreamor::makeSet<ThingCol>(things).collect();
  const dreamor::concert::VarTable<ThingCol> t{env, sTHINGS, 0, 1, ILOFLOAT, "t"};
  DREAMOR_CHECK(std::string{t(ThingCol{things.data()}).getName()} == "t_");

  // text tokens - a keyName() returning a std::string - written as they are, a name too long cut
  // at the buffer (and a warning)
  const std::vector<named::Thing> named_things{{"M3"}, {std::string(300, 'z')}};
  const std::vector<std::size_t> seven{7};
  const auto sNAMED = dreamor::makeSet<NamedCol>(named_things).collect();
  const dreamor::concert::VarTable<NamedCol, JCol> vNamed{
      env, dreamor::makeSet<JCol>(sNAMED, seven).collect(), 0, 1, ILOFLOAT, "n"};
  DREAMOR_CHECK(std::string{vNamed(NamedCol{named_things.data()}, JCol{7}).getName()} == "n_M3_7");
  const std::string cut{vNamed(NamedCol{&named_things[1]}, JCol{7}).getName()};
  DREAMOR_CHECK(cut.size() == 255 && cut == "n_" + std::string(253, 'z'));
  return report("vartable");
} catch (...) {
  return ::uncaught();
}
