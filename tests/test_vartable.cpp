// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// VarTable: built once from a set, bounds as numbers or column callables, names from keys,
// lookup, iteration in set order, solution().
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
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"
using cI = dreamor::Col<std::size_t, "i">;
using cJ = dreamor::Col<std::size_t, "j">;
using cK = dreamor::Col<std::size_t, "k">;

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
  template <typename FormatContext>
  static auto format(const throwing::Token& /*token*/, FormatContext& ctx) -> decltype(ctx.out()) {
    throw std::runtime_error("this token cannot be formatted");
  }
};
using cThing = dreamor::Col<const throwing::Thing*, "thing">;
using cNamed = dreamor::Col<const named::Thing*, "named">;

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

}  // namespace

int main() try {
  const IloEnv env;
  const std::vector<std::size_t> I{3, 1, 2};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::makeSet<cI, cJ>(I, J).collect();

  static_assert(!std::is_copy_constructible_v<dreamor::concert::VarTable<cI, cJ>>);
  static_assert(std::is_nothrow_move_constructible_v<dreamor::concert::VarTable<cI, cJ>>);
  static_assert(!std::is_default_constructible_v<dreamor::concert::VarTable<cI, cJ>>);

  // numbers
  const dreamor::concert::VarTable<cI, cJ> x{env, IJ, 0, 1, ILOINT, "x"};
  DREAMOR_CHECK(x.size() == 6 && x.keys().begin() == IJ.begin());
  DREAMOR_CHECK(x(cI{1}, cJ{20}).getImpl() != nullptr && x.contains(cI{1}, cJ{20}));
  DREAMOR_CHECK(!x.contains(cI{9}, cJ{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(cI{9}, cJ{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(std::tuple{cI{9}, cJ{9}}));
  DREAMOR_CHECK(std::string{x(cI{1}, cJ{20}).getName()} == "x_1_20");
  DREAMOR_CHECK(exactlyEqual(x(cI{3}, cJ{10}).getLB(), 0) && exactlyEqual(x(cI{3}, cJ{10}).getUB(), 1));

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
  const auto IJ2 = dreamor::makeSet<cI, cJ>(I2, J).collect();
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
    std::vector<dreamor::SetOf<cI, cJ>> sets;
    sets.reserve(6);
    for (std::size_t s = 0; s < 6; ++s) {
      const std::vector<std::size_t> Is{s + round, s + round + 1};
      sets.push_back(dreamor::makeSet<cI, cJ>(Is, J).collect());
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
  const auto IJK = dreamor::makeSet<cI, cJ, cK>(V, V, V).collect();
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
  const auto IJ3           = dreamor::makeSet<cI, cJ>(I3, J).collect();
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

  // a ValueTable of data on a set (param {IJ}): a number for every row, or a callable over the row's columns;
  // it shares the set's row index with the tables on it, and reads like a solution
  const dreamor::ValueTable<cI, cJ> ones{IJ, 1};
  const auto i_plus_j = [](cI i, cJ j) { return static_cast<double>(i.get() + j.get()); };
  const dreamor::ValueTable<cI, cJ> sums{IJ, i_plus_j};
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
  DREAMOR_CHECK(exactlyEqual(dreamor::ValueTable<cI, cJ>(IJ, ten_i)(cI{3}, cJ{10}), 30.0));
  DREAMOR_CHECK(dreamor::ValueTable<cI, cJ>(IJ.where([](cI i) { return i.get() > 100; }), 1.0).size() == 0);
  // a ValueTable is a column callable: a VarTable's bound, and the value of another ValueTable on fewer or more columns
  const dreamor::concert::VarTable<cI, cJ> capped{env, IJ, 0, sums};
  DREAMOR_CHECK(exactlyEqual(capped(cI{2}, cJ{20}).getUB(), 22.0) && exactlyEqual(capped(cI{3}, cJ{10}).getUB(), 13.0));
  const auto II                          = dreamor::makeSet<cI>(I).collect();
  const dreamor::ValueTable<cI> tens     = {II, ten_i};
  const dreamor::ValueTable<cI, cJ> wide = {IJ, tens};  // reads the cI of each row
  DREAMOR_CHECK(exactlyEqual(wide(cI{2}, cJ{20}), 20.0) && exactlyEqual(wide(cI{3}, cJ{10}), 30.0));

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
  const std::vector<throwing::Thing> things(1);
  const auto sTHINGS = dreamor::makeSet<cThing>(things).collect();
  const dreamor::concert::VarTable<cThing> t{env, sTHINGS, 0, 1, ILOFLOAT, "t"};
  DREAMOR_CHECK(std::string{t(cThing{things.data()}).getName()} == "t_");

  // text tokens - a keyName() returning a std::string - written as they are, a name too long cut
  // at the buffer (and a warning)
  const std::vector<named::Thing> named_things{{"M3"}, {std::string(300, 'z')}};
  const std::vector<std::size_t> seven{7};
  const auto sNAMED = dreamor::makeSet<cNamed>(named_things).collect();
  const dreamor::concert::VarTable<cNamed, cJ> vNamed{
      env, dreamor::makeSet<cJ>(sNAMED, seven).collect(), 0, 1, ILOFLOAT, "n"};
  DREAMOR_CHECK(std::string{vNamed(cNamed{named_things.data()}, cJ{7}).getName()} == "n_M3_7");
  // a column under std::format prints the token its variable's name has
  DREAMOR_CHECK(std::format("n_{}_{}", cNamed{named_things.data()}, cJ{7}) ==
                vNamed(cNamed{named_things.data()}, cJ{7}).getName());
  const std::string cut{vNamed(cNamed{&named_things[1]}, cJ{7}).getName()};
  DREAMOR_CHECK(cut.size() == 255 && cut == "n_" + std::string(253, 'z'));
  return report("vartable");
} catch (...) { return ::uncaught(); }
