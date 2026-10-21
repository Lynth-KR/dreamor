// SPDX-License-Identifier: Apache-2.0
// VarTable: built once from a set, bounds as numbers or column callables, names from keys,
// lookup, iteration in set order, solution().
#include <cstddef>
#include <cstring>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
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
namespace throwing {
struct Token {};
struct Thing {};
inline Token keyName(const Thing* /*thing*/) {
  return {};
}
}  // namespace throwing
template <>
struct std::formatter<throwing::Token> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
  auto format(const throwing::Token& /*token*/, std::format_context& ctx) const -> decltype(ctx.out()) {
    throw std::runtime_error("this token cannot be formatted");
  }
};
using ThingCol = dreamor::Col<const throwing::Thing*, "thing">;

// Stands in for IloCplex: getEnv() and getValues(IloNumArray, IloNumVarArray).
struct FakeAlgorithm {
  IloEnv env;
  IloEnv getEnv() const { return env; }
  // "Solution" = lb + ub, so each value is checkable from the variable itself.
  void getValues(IloNumArray out, const IloNumVarArray vars) const {
    for (IloInt i = 0; i < vars.getSize(); ++i) out.add(vars[i].getLB() + vars[i].getUB());
  }
};

int main() {
  IloEnv env;
  const std::vector<std::size_t> I{3, 1, 2}, J{10, 20};
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

  // a set that repeats a row is refused
  DREAMOR_CHECK_THROWS(std::invalid_argument,
                       dreamor::concert::VarTable<ICol, JCol>{env, dreamor::concat(IJ, IJ), 0, 1});

  // solution: one call, values keyed like the table, temporaries ended
  const std::size_t arraysBefore = IloNumVarArray::ended, numsBefore = IloNumArray::ended;
  const auto v = y.solution(FakeAlgorithm{env});
  DREAMOR_CHECK(IloNumVarArray::ended == arraysBefore + 1 && IloNumArray::ended == numsBefore + 1);
  DREAMOR_CHECK(v.size() == 6 && exactlyEqual(v(ICol{2}, JCol{20}), -2 + 40) && v.contains(ICol{1}, JCol{10}));
  DREAMOR_CHECK_THROWS(std::out_of_range, v(ICol{9}, JCol{9}));
  std::size_t m = 0;
  for (const auto& [row, value] : v) {
    DREAMOR_CHECK(exactlyEqual(value, y(row).getLB() + y(row).getUB()));
    ++m;
  }
  DREAMOR_CHECK(m == 6);

  // empty set: nothing to read, no solver call needed
  const auto none = IJ.where([](ICol i) { return i.get() > 100; });
  const dreamor::concert::VarTable<ICol, JCol> e{env, none, 0, 1};
  DREAMOR_CHECK(e.size() == 0 && e.solution(FakeAlgorithm{env}).size() == 0 && e.begin() == e.end());

  // move keeps lookups working
  dreamor::concert::VarTable<ICol, JCol> moved{env, IJ, 0, 1, ILOINT, "w"};
  const auto before = moved(ICol{1}, JCol{10}).getImpl();
  const dreamor::concert::VarTable<ICol, JCol> moved2{std::move(moved)};
  DREAMOR_CHECK(moved2(ICol{1}, JCol{10}).getImpl() == before);

  // a name token whose formatter throws: the name stops after the separator (and a warning)
  const std::vector<throwing::Thing> things(1);
  const auto sTHINGS = dreamor::makeSet<ThingCol>(things).collect();
  const dreamor::concert::VarTable<ThingCol> t{env, sTHINGS, 0, 1, ILOFLOAT, "t"};
  DREAMOR_CHECK(std::string{t(ThingCol{things.data()}).getName()} == "t_");
  return report("vartable");
}
