// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Cross-backend differential test: one random model, written once against a backend's side, built on the Concert
// stub and on another backend, the two models read back and compared exactly. A side is the backend's VarTable,
// sumExpr and row; writeModel is the same code for every side, so whatever differs comes from the backend.
//
// A case draws a set as stress/differential.hpp draws it - a chain of a fixed source, a cross, a lateral and two
// where's, over values 0..kValues-1 - and on it four tables: x on the set (a number below, a column callable above), w
// on the set with every default (no name, so it is in no sum), y on a projection in another column order (a ParamTable
// below, a number or infinity above), z on the first column (a column callable below, a ParamTable from records
// above); each but w continuous or integer. Then sums, each read back as an expression and added as a row with a
// random sense and right-hand side: a table as the term, a column times a variable plus a value of the row times
// another, a term with a constant, where and whereEach, a ParamTable's value times a variable, a container's sum, a
// column and an int as the term, the slices of a group of the table's own set and of another set with the same rows,
// a two-column group; then a fifth table, v, on that other set, and the sum of v over the first set. One of the sums
// is the objective, minimized or maximized, set before v's columns exist or after; and a sum over rows outside x's
// set, which every backend throws on. The stub checks each name and bound against the key and the bound's source,
// since names and bounds are made by code every backend shares.
//
// Compared: the variables in the order made, by name with their bounds and type; each sum as a map from variable
// name to coefficient, with its constant; each row by name with its bounds and terms; the objective and its sense.
// Terms over one variable are added in the order the backend holds them and zeros left out, as the backends merge
// a row. The coefficients are tenths, not exact in binary, and every backend adds them in the same order, so the
// comparison is exact: a term never names a variable twice, which would let one backend add two of its coefficients
// together before the rest.
//
// DREAMOR_DIFF_SEED and DREAMOR_DIFF_CASES replace the seed and the number of cases, as in stress/differential.hpp; a
// failed case prints itself, its index and the seed.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <random>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include <ilcplex/ilocplex.h>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/param_table.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

namespace cross_backend {

inline constexpr std::size_t kValues = 5;  // every column takes values 0..kValues-1; kValues is in no set

inline constexpr std::uint32_t kSeed = 20261110;
inline constexpr std::size_t kCases  = 300;

// The draws are differential.hpp's, kept apart from it so that the two tests change on their own.

// A list of distinct values in a random order, sized to hit the edges often: empty and single lists, then longer
// ones up to every value.
inline std::vector<std::size_t> randomList(std::mt19937& g) {
  std::uniform_int_distribution<std::size_t> shape(0, 9);
  std::uniform_int_distribution<std::size_t> longer(2, kValues);
  const std::size_t pick = shape(g);
  std::size_t size       = pick;  // 0 and 1 as they come
  if (pick >= 2) { size = longer(g); }
  std::vector<std::size_t> out(kValues);
  std::iota(out.begin(), out.end(), std::size_t{0});
  std::shuffle(out.begin(), out.end(), g);
  out.resize(size);
  return out;
}

// (x + y) % modulus != residue - or a predicate that keeps everything (modulus 1, residue 1) or nothing (modulus 1,
// residue 0).
struct Rule {
  std::size_t modulus, residue;

  [[nodiscard]] bool keeps(std::size_t x, std::size_t y) const { return (x + y) % modulus != residue; }
};

inline Rule randomRule(std::mt19937& g) {
  std::uniform_int_distribution<std::size_t> modulus(1, 4);
  const std::size_t m = modulus(g);
  std::uniform_int_distribution<std::size_t> residue(0, m == 1 ? 1 : m - 1);
  return {m, residue(g)};
}

struct Data {
  std::vector<std::size_t> as, bs;
  std::vector<std::vector<std::size_t>> cs_of;  // the lateral column: cs_of[a]
  Rule ac, b_only;
};

inline Data randomData(std::mt19937& g) {
  Data d{randomList(g), randomList(g), {}, randomRule(g), randomRule(g)};
  for (std::size_t a = 0; a < kValues; ++a) {
    d.cs_of.push_back(randomList(g));
  }
  return d;
}

// The variable's text, if it is set. The MSVC CRT deprecates getenv, so Windows takes _dupenv_s.
[[nodiscard]] inline std::optional<std::string> environmentText(const char* variable) {
#ifdef _WIN32
  char* value      = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, variable) != 0 || value == nullptr) {
    std::free(value);
    return std::nullopt;
  }
  std::string text{value};
  std::free(value);
  return text;
#else
  const char* const value = std::getenv(variable);
  if (value == nullptr) { return std::nullopt; }
  return std::string{value};
#endif
}

// A number from the environment, if the variable is set; one that does not read as a number stops the run rather
// than test something other than what was asked.
template <typename T>
[[nodiscard]] std::optional<T> fromEnvironment(const char* variable) {
  const std::optional<std::string> text = environmentText(variable);
  if (!text.has_value()) { return std::nullopt; }
  const std::string_view view{*text};
  T value{};
  const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
  if (view.empty() || error != std::errc{} || end != view.data() + view.size()) {
    throw std::invalid_argument(std::string{variable} + " is not a number: \"" + std::string{view} + "\"");
  }
  return value;
}

// Three integer types, so that the names write each. cA holds index - 1, so a name has a negative value too.
DREAMOR_VALUE_COLUMN(cA, int);
DREAMOR_VALUE_COLUMN(cB, std::size_t);
DREAMOR_VALUE_COLUMN(cC, std::uint32_t);

[[nodiscard]] inline int aValue(std::size_t i) {
  return static_cast<int>(i) - 1;
}

[[nodiscard]] inline std::size_t indexOf(cA a) {
  return static_cast<std::size_t>(a.get()) + 1;  // -1 + 1 is 0, unsigned arithmetic wrapping
}

[[nodiscard]] inline std::size_t indexOf(cB b) {
  return b.get();
}

[[nodiscard]] inline std::size_t indexOf(cC c) {
  return c.get();
}

enum class Sense : std::uint8_t { kLess, kGreater, kEqual };

// The sums writeModel adds as rows: eleven over whole sets, three for each key of cA and one past them, one
// two-column group, and one of a table made after the others.
inline constexpr std::size_t kFixedSums = 11;
inline constexpr std::size_t kRows      = kFixedSums + (3 * (kValues + 1)) + 2;
// The points between which a side may hand its model over: before anything, after each of the five tables, after
// each row.
inline constexpr std::size_t kSteps = 6 + kRows;

struct Case {
  Data data;
  std::vector<double> wa, wb, wc;  // a weight for each index 0..kValues, in tenths
  Rule keep, first, second;
  double x_lb;
  bool x_lb_infinite, x_integer;
  double y_ub;
  bool y_ub_infinite, y_integer, z_integer;
  std::size_t b_key, c_key;  // the key of the two-column group summed
  std::vector<Sense> senses;
  std::vector<double> rhs;
  std::size_t objective;  // which row's sum: one of the first kFixedSums, or the last
  bool maximize;
  std::size_t hand_over_at;  // a step, for a side that hands its model over
};

inline Case randomCase(std::mt19937& g) {
  std::uniform_int_distribution<int> tenths(-20, 20);
  std::uniform_int_distribution<int> coin(0, 1);
  std::uniform_int_distribution<int> sense(0, 2);
  std::uniform_int_distribution<std::size_t> value(0, kValues - 1);
  std::uniform_int_distribution<std::size_t> objective(0, kFixedSums);
  std::uniform_int_distribution<std::size_t> step(0, kSteps);
  const auto weights = [&] {
    std::vector<double> out(kValues + 1);
    for (double& w : out) {
      w = tenths(g) * 0.1;
    }
    return out;
  };
  Case k{
      .data          = randomData(g),
      .wa            = weights(),
      .wb            = weights(),
      .wc            = weights(),
      .keep          = randomRule(g),
      .first         = randomRule(g),
      .second        = randomRule(g),
      .x_lb          = -std::abs(tenths(g) * 0.1),
      .x_lb_infinite = coin(g) == 1,
      .x_integer     = coin(g) == 1,
      .y_ub          = std::abs(tenths(g) * 0.1),
      .y_ub_infinite = coin(g) == 1,
      .y_integer     = coin(g) == 1,
      .z_integer     = coin(g) == 1,
      .b_key         = value(g),
      .c_key         = value(g),
      .senses        = {},
      .rhs           = {},
      .objective     = std::min(objective(g), kRows - 1),
      .maximize      = coin(g) == 1,
      .hand_over_at  = step(g),
  };
  for (std::size_t r = 0; r < kRows; ++r) {
    k.senses.push_back(static_cast<Sense>(sense(g)));
    k.rhs.push_back(tenths(g) * 0.25);
  }
  return k;
}

// The model as read back from a backend.
struct Variable {
  std::string name;
  double lower, upper;
  bool integer;
};

struct Linear {
  std::map<std::string, double> terms;  // no zero
  double constant = 0;
};

struct Row {
  std::string name;
  double lower, upper;
  std::map<std::string, double> terms;  // no zero
};

struct Readout {
  std::vector<Variable> variables;
  std::vector<std::string> labels;  // of each sum, for a message
  std::vector<Linear> sums;
  std::vector<Row> rows;
  Linear objective;
  bool maximize = false;
  bool threw    = false;  // the sum over rows outside x's set threw std::out_of_range
};

// A bound past 1e20 is infinite: Concert's IloInfinity is 1e20, as HiGHS's default infinite_bound.
[[nodiscard]] inline double bound(double x) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  if (x >= 1e20) { return kInf; }
  if (x <= -1e20) { return -kInf; }
  return x;
}

// Adds up the terms over one name in the order given, then leaves the zeros out.
template <typename Terms>
[[nodiscard]] std::map<std::string, double> merged(const Terms& terms) {
  std::map<std::string, double> out;
  for (const auto& [name, coefficient] : terms) {
    out[std::string{name}] += coefficient;
  }
  std::erase_if(out, [](const auto& entry) { return isExactlyZero(entry.second); });
  return out;
}

// The case's set: as x bs x cs_of(a), where ac(a, c) and b_only(b).
[[nodiscard]] inline auto buildSet(const Data& d) {
  std::vector<int> as;
  as.reserve(d.as.size());
  for (const std::size_t i : d.as) {
    as.push_back(aValue(i));
  }
  std::vector<std::vector<std::uint32_t>> cs_of;
  for (const auto& list : d.cs_of) {
    cs_of.emplace_back();
    for (const std::size_t c : list) {
      cs_of.back().push_back(static_cast<std::uint32_t>(c));
    }
  }
  const auto cs  = [&cs_of](cA a) { return std::span<const std::uint32_t>(cs_of[indexOf(a)]); };
  const auto ac  = [&d](cA a, cC c) { return d.ac.keeps(indexOf(a), indexOf(c)); };
  const auto bok = [&d](cB b) { return d.b_only.keeps(indexOf(b), 0); };
  return dreamor::index_set::relational::from<cA>(as).cross<cB>(d.bs).lateral<cC>(cs).where(ac).where(bok).build();
}

// <name>_<key0>_<key1>..., each value as std::to_string writes it; no name if the family has none.
template <typename Tuple>
[[nodiscard]] std::string expectedName(std::string_view family, const Tuple& row) {
  if (family.empty()) { return {}; }
  std::string name{family};
  std::apply([&name](const auto&... column) { ((name += "_" + std::to_string(column.get())), ...); }, row);
  return name;
}

struct ZRecord {
  int a;
  double upper;
};

// The model, on any side. A side has: infinity(); table(set, lb, ub, integer, name) and defaults(set), each a
// VarTable; record(table, name, bounds_of), for a side whose model lists no variables, with what the table's name and
// bounds should be; sum(rows, term) and sumOf<Column>(container,
// term), the backend's sumExpr; readSum(label, expression), addRow(expression, sense, rhs, name) and
// objective(expression, maximize); step(), called before anything and after each table and row; out, its Readout.
template <typename Side>
void writeModel(Side& side, const Case& k) {
  const Data& d = k.data;
  const auto S  = buildSet(d);
  const auto S2 = buildSet(d);  // the same rows, held apart: its groups are another set's
  const auto P  = S.select<cC, cA>();
  std::vector<int> a_values;
  a_values.reserve(d.as.size());
  for (const std::size_t i : d.as) {
    a_values.push_back(aValue(i));
  }
  const auto A = dreamor::index_set::relational::from<cA>(a_values).build();
  side.step();

  const double x_lb = k.x_lb_infinite ? -Side::infinity() : k.x_lb;
  const auto x_ub   = [&k](cA a, cC c) { return 1.0 + std::abs(k.wa[indexOf(a)]) + std::abs(k.wc[indexOf(c)]); };
  const auto x      = side.table(S, x_lb, x_ub, k.x_integer, "x");
  side.record(x, "x", [&](const auto& r) { return std::pair{x_lb, x_ub(std::get<cA>(r), std::get<cC>(r))}; });
  side.step();
  const auto w = side.defaults(S);
  side.record(w, "", [](const auto& /*r*/) { return std::pair{0.0, Side::infinity()}; });
  side.step();
  const auto y_lb_of = [&k](cC c, cA a) { return -std::abs(k.wc[indexOf(c)] - k.wa[indexOf(a)]); };
  const dreamor::ParamTable<double, cC, cA> y_lb{P, y_lb_of};
  const double y_ub = k.y_ub_infinite ? Side::infinity() : k.y_ub;
  const auto y      = side.table(P, y_lb, y_ub, k.y_integer, "y");
  side.record(y, "y", [&](const auto& r) { return std::pair{y_lb_of(std::get<cC>(r), std::get<cA>(r)), y_ub}; });
  side.step();
  std::vector<ZRecord> z_records;  // A's rows in reverse: the table's lookups find another order
  for (const std::size_t i : std::views::reverse(d.as)) {
    z_records.push_back(ZRecord{.a = aValue(i), .upper = 0.5 + std::abs(k.wb[i])});
  }
  const auto z_row_of   = [](const ZRecord& r) { return std::tuple<cA>{cA{r.a}}; };
  const auto z_upper_of = [](const ZRecord& r) { return r.upper; };
  const dreamor::ParamTable<double, cA> z_ub{z_records, z_row_of, z_upper_of};
  const auto z_lb     = [&k](cA a) { return -std::abs(k.wa[indexOf(a)]); };
  const auto z        = side.table(A, z_lb, z_ub, k.z_integer, "z");
  const auto z_bounds = [&](const auto& r) {
    const cA a = std::get<cA>(r);
    return std::pair{z_lb(a), 0.5 + std::abs(k.wb[indexOf(a)])};
  };
  side.record(z, "z", z_bounds);
  side.step();

  // Each term names a variable once, so that no backend adds two of its coefficients before the rest.
  const auto mul      = [&](cA a, cB b, cC c) { return c * x(a, b, c) + k.wb[indexOf(b)] * y(c, a); };
  const auto shifted  = [&](cA a, cC c) { return k.wa[indexOf(a)] * z(a) - k.wc[indexOf(c)]; };
  const auto price_of = [&k](cC c, cA a) { return k.wc[indexOf(c)] + k.wa[indexOf(a)]; };
  const dreamor::ParamTable<double, cC, cA> price{P, price_of};
  const auto priced = [&](cA a, cB b, cC c) { return price(c, a) * x(a, b, c); };
  const auto keep   = [&k](cA a, cB b) { return k.keep.keeps(indexOf(a), indexOf(b)); };
  const auto first  = [&k](cB b, cC c) { return k.first.keeps(indexOf(b), indexOf(c)); };
  const auto second = [&k](cA a, cC c) { return k.second.keeps(indexOf(a), indexOf(c)); };

  std::size_t row = 0;
  const auto add  = [&](std::string label, const auto& expression) {
    side.readSum(std::move(label), expression);
    side.addRow(expression, k.senses[row], k.rhs[row], "r" + std::to_string(row));
    if (row == k.objective) { side.objective(expression, k.maximize); }
    ++row;
    side.step();
  };
  add("sum(S, x)", side.sum(S, x).build());
  add("sum(S, c * x + wb * y)", side.sum(S, mul).build());
  add("sum(S, wa * z - wc)", side.sum(S, shifted).build());
  auto split = side.sum(S, x).where(keep).whereEach(first, second).build();
  add("sum(S, x).where(keep).whereEach(first, ...)[0]", split[0]);
  add("sum(S, x).where(keep).whereEach(..., second)[1]", split[1]);
  add("sum(S, price * x)", side.sum(S, priced).build());
  add("sum(P, y)", side.sum(P, y).build());
  add("sum<cA>(as, z)", side.template sumOf<cA>(a_values, z).build());
  add("sum(S, c * x + wb * y).where(keep)", side.sum(S, mul).where(keep).build());
  add("sum(S, c)", side.sum(S, [](cC c) { return c; }).build());                // a column of numbers, no variable
  add("sum(S, 2 * a)", side.sum(S, [](cA a) { return 2 * a.get(); }).build());  // an int
  const auto byA  = S.groupBy<cA>();
  const auto byA2 = S2.groupBy<cA>();
  for (std::size_t i = 0; i <= kValues; ++i) {  // kValues: a key no set has
    const cA a{aValue(i)};
    const std::string key = std::to_string(a.get());
    add("sum(byA.slice(" + key + "), x)", side.sum(byA.slice(a), x).build());
    add("sum(byA.slice(" + key + "), c * x + wb * y)", side.sum(byA.slice(a), mul).build());
    add("sum(byA2.slice(" + key + "), x)", side.sum(byA2.slice(a), x).build());
  }
  const auto byBC = S.groupBy<cB, cC>();
  add("sum(byBC.slice(" + std::to_string(k.b_key) + ", " + std::to_string(k.c_key) + "), x)",
      side.sum(byBC.slice(cB{k.b_key}, cC{static_cast<std::uint32_t>(k.c_key)}), x).build());

  // A table made after the rows and the objective, on S2: its columns come after the objective's costs, and a sum
  // over S's rows finds each of its variables by the row's columns, the row not being one of S2's.
  const double v_ub = k.y_ub_infinite ? Side::infinity() : k.y_ub;
  const auto v      = side.table(S2, -1.0, v_ub, !k.x_integer, "v");
  side.record(v, "v", [&](const auto& /*r*/) { return std::pair{-1.0, v_ub}; });
  side.step();
  add("sum(S, v), v on S2", side.sum(S, v).build());
  DREAMOR_CHECK(row == kRows);

  // Every row of as x bs x every value: outside x's set unless the lateral and the where's keep all.
  std::vector<std::uint32_t> every_c;
  every_c.reserve(kValues);
  for (std::size_t c = 0; c < kValues; ++c) {
    every_c.push_back(static_cast<std::uint32_t>(c));
  }
  const auto R = dreamor::index_set::relational::from<cA>(a_values).cross<cB>(d.bs).cross<cC>(every_c).build();
  try {
    static_cast<void>(side.sum(R, x).build());
  } catch (const std::out_of_range&) { side.out.threw = true; }
  DREAMOR_CHECK(side.out.threw == !dreamor::within(R, S));
}

// The Concert side, on the stub. The stub's model lists its constraints, not its variables or its objective, so
// the tables are recorded as they are made and the objective is the sum's expression.
struct ConcertSide {
  IloEnv env;
  IloModel model{env};
  Readout out;

  [[nodiscard]] static double infinity() { return IloInfinity; }

  template <typename... Keys, typename Lb, typename Ub>
  [[nodiscard]] dreamor::concert::VarTable<Keys...> table(
      const dreamor::IndexSet<Keys...>& set, const Lb& lb, const Ub& ub, bool integer, std::string_view name) {
    return {env, set, lb, ub, integer ? ILOINT : ILOFLOAT, name};
  }

  template <typename... Keys>
  [[nodiscard]] dreamor::concert::VarTable<Keys...> defaults(const dreamor::IndexSet<Keys...>& set) {
    return {env, set};
  }

  // Reads the table's variables, and checks them against the name and the bounds they should have: the names and
  // bounds are made by code every backend shares, which a comparison of two backends would not see go wrong.
  template <typename Table, typename BoundsOf>
  void record(const Table& table, std::string_view name, const BoundsOf& bounds_of) {
    for (const auto& [row, var] : table) {
      const IloNumVarI* v       = var.getImpl();
      const auto [lower, upper] = bounds_of(row);
      DREAMOR_CHECK(v->name == expectedName(name, row));
      DREAMOR_CHECK(exactlyEqual(bound(v->lb), bound(lower)) && exactlyEqual(bound(v->ub), bound(upper)));
      out.variables.push_back(Variable{v->name, bound(v->lb), bound(v->ub), v->type == IloNumVar::Int});
    }
  }

  template <typename Rows, typename Term>
  [[nodiscard]] auto sum(const Rows& rows, const Term& term) {
    return dreamor::concert::sumExpr(env, rows, term);
  }

  template <typename Column, typename Rows, typename Term>
  [[nodiscard]] auto sumOf(const Rows& rows, const Term& term) {
    return dreamor::concert::sumExpr<Column>(env, rows, term);
  }

  [[nodiscard]] static Linear linearOf(const IloExpr& e) {
    DREAMOR_CHECK(e.expr_->quadratic.empty());
    std::vector<std::pair<std::string_view, double>> terms;
    terms.reserve(e.expr_->terms.size());
    for (const auto& [v, k] : e.expr_->terms) {
      terms.emplace_back(v->name, k);
    }
    return {merged(terms), e.expr_->constant};
  }

  void readSum(std::string label, const IloExpr& e) {
    out.labels.push_back(std::move(label));
    out.sums.push_back(linearOf(e));
  }

  void addRow(const IloExpr& e, Sense sense, double rhs, const std::string& name) const {
    if (sense == Sense::kLess) {
      model.add(e <= rhs).setName(name.c_str());
    } else if (sense == Sense::kGreater) {
      model.add(e >= rhs).setName(name.c_str());
    } else {
      model.add(e == rhs).setName(name.c_str());
    }
  }

  void objective(const IloExpr& e, bool maximize) {
    model.add(maximize ? IloMaximize(env, e) : IloMinimize(env, e));
    out.objective = linearOf(e);
    out.maximize  = maximize;
  }

  void step() {}

  // The rows, read out of the model; the rest was read as it was made.
  void finish() {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    for (const IloConstraintI* c : *model.cs_) {
      std::vector<std::pair<std::string_view, double>> terms;
      terms.reserve(c->terms.size());
      for (const auto& [v, k] : c->terms) {
        terms.emplace_back(v->name, k);
      }
      // sense: -1 is terms <= rhs, 0 is terms == rhs, 1 is terms >= rhs
      out.rows.push_back(Row{
          .name  = c->name,
          .lower = c->sense >= 0 ? bound(c->rhs) : -kInf,
          .upper = c->sense <= 0 ? bound(c->rhs) : kInf,
          .terms = merged(terms),
      });
    }
  }
};

[[nodiscard]] inline std::string number(double x) {
  std::array<char, 64> buf{};
  std::snprintf(buf.data(), buf.size(), "%.17g", x + 0.0);  // + 0.0: -0 and 0 print alike
  return buf.data();
}

[[nodiscard]] inline std::string text(const std::map<std::string, double>& terms) {
  std::string s;
  for (const auto& [name, coefficient] : terms) {
    s += " " + number(coefficient) + "*" + name;
  }
  return s;
}

[[nodiscard]] inline bool same(const std::map<std::string, double>& a, const std::map<std::string, double>& b) {
  return std::ranges::equal(
      a, b, [](const auto& l, const auto& r) { return l.first == r.first && exactlyEqual(l.second, r.second); });
}

[[nodiscard]] inline bool same(const Linear& a, const Linear& b) {
  return same(a.terms, b.terms) && exactlyEqual(a.constant, b.constant);
}

[[nodiscard]] inline std::string text(const Linear& e) {
  return number(e.constant) + " +" + text(e.terms);
}

[[nodiscard]] inline std::string text(const Variable& v) {
  return v.name + " in [" + number(v.lower) + ", " + number(v.upper) + "]" + (v.integer ? " integer" : "");
}

[[nodiscard]] inline std::string text(const Row& r) {
  return r.name + ": " + number(r.lower) + " <=" + text(r.terms) + " <= " + number(r.upper);
}

// Compares a side's model with the stub's, and prints the first difference of each kind.
[[nodiscard]] inline bool sameModel(const Readout& stub, const Readout& other, const char* side) {
  bool ok           = true;
  const auto differ = [&](const char* what, std::size_t at, const std::string& a, const std::string& b) {
    std::fprintf(stderr, "  %s %s %zu:\n    concert: %s\n    %s: %s\n", side, what, at, a.c_str(), side, b.c_str());
    ok = false;
  };
  if (stub.variables.size() != other.variables.size()) {
    differ("variables, count", 0, std::to_string(stub.variables.size()), std::to_string(other.variables.size()));
  }
  for (std::size_t i = 0; i < std::min(stub.variables.size(), other.variables.size()); ++i) {
    const Variable& a = stub.variables[i];
    const Variable& b = other.variables[i];
    if (a.name != b.name || !exactlyEqual(a.lower, b.lower) || !exactlyEqual(a.upper, b.upper) ||
        a.integer != b.integer) {
      differ("variable", i, text(a), text(b));
      break;
    }
  }
  if (stub.sums.size() != other.sums.size()) {
    differ("sums, count", 0, std::to_string(stub.sums.size()), std::to_string(other.sums.size()));
  }
  for (std::size_t i = 0; i < std::min(stub.sums.size(), other.sums.size()); ++i) {
    if (!same(stub.sums[i], other.sums[i])) {
      differ(("sum " + stub.labels[i] + ",").c_str(), i, text(stub.sums[i]), text(other.sums[i]));
      break;
    }
  }
  if (stub.rows.size() != other.rows.size()) {
    differ("rows, count", 0, std::to_string(stub.rows.size()), std::to_string(other.rows.size()));
  }
  for (std::size_t i = 0; i < std::min(stub.rows.size(), other.rows.size()); ++i) {
    const Row& a = stub.rows[i];
    const Row& b = other.rows[i];
    if (a.name != b.name || !exactlyEqual(a.lower, b.lower) || !exactlyEqual(a.upper, b.upper) ||
        !same(a.terms, b.terms)) {
      differ("row", i, text(a), text(b));
      break;
    }
  }
  if (!same(stub.objective, other.objective) || stub.maximize != other.maximize) {
    differ("objective",
           0,
           text(stub.objective) + (stub.maximize ? " max" : " min"),
           text(other.objective) + (other.maximize ? " max" : " min"));
  }
  if (stub.threw != other.threw) {
    differ("out_of_range thrown", 0, stub.threw ? "yes" : "no", other.threw ? "yes" : "no");
  }
  return ok;
}

[[nodiscard]] inline std::string text(const std::vector<std::size_t>& list) {
  std::string s = "{";
  for (const std::size_t v : list) {
    s += (s.size() > 1 ? "," : "") + std::to_string(v);
  }
  return s + "}";
}

[[nodiscard]] inline std::string text(const std::vector<double>& list) {
  std::string s = "{";
  for (const double v : list) {
    s += (s.size() > 1 ? "," : "") + number(v);
  }
  return s + "}";
}

[[nodiscard]] inline std::string text(const Rule& r) {
  return "(x+y)%" + std::to_string(r.modulus) + "!=" + std::to_string(r.residue);
}

inline void describe(const Case& k) {
  const Data& d = k.data;
  std::string cs;
  for (std::size_t a = 0; a < d.cs_of.size(); ++a) {
    cs += " " + std::to_string(aValue(a)) + ":" + text(d.cs_of[a]);
  }
  std::fprintf(stderr,
               "  the case (A holds index - 1): as %s, bs %s, cs_of%s; where ac %s, b %s\n"
               "  wa %s, wb %s, wc %s; keep %s, first %s, second %s\n"
               "  x lb %s%s, y ub %s%s, integer x %d y %d z %d; byBC key (%zu, %zu); objective sum %zu %s; "
               "hand-over at step %zu\n",
               text(d.as).c_str(),
               text(d.bs).c_str(),
               cs.c_str(),
               text(d.ac).c_str(),
               text(d.b_only).c_str(),
               text(k.wa).c_str(),
               text(k.wb).c_str(),
               text(k.wc).c_str(),
               text(k.keep).c_str(),
               text(k.first).c_str(),
               text(k.second).c_str(),
               number(k.x_lb).c_str(),
               k.x_lb_infinite ? " (-inf)" : "",
               number(k.y_ub).c_str(),
               k.y_ub_infinite ? " (inf)" : "",
               static_cast<int>(k.x_integer),
               static_cast<int>(k.y_integer),
               static_cast<int>(k.z_integer),
               k.b_key,
               k.c_key,
               k.objective,
               k.maximize ? "max" : "min",
               k.hand_over_at);
}

// The whole test: compare(case, stub's readout) builds the case on the other backend and compares, true if equal.
template <typename Compare>
[[nodiscard]] int run(const char* name, const Compare& compare) {
  const std::optional<std::uint32_t> seed_set = fromEnvironment<std::uint32_t>("DREAMOR_DIFF_SEED");
  const std::optional<std::size_t> cases_set  = fromEnvironment<std::size_t>("DREAMOR_DIFF_CASES");
  const std::uint32_t seed                    = seed_set.value_or(kSeed);
  const std::size_t cases                     = cases_set.value_or(kCases);
  if (seed_set) { std::printf("  %s: seed %u, from DREAMOR_DIFF_SEED\n", name, seed); }
  if (cases_set) { std::printf("  %s: %zu cases, from DREAMOR_DIFF_CASES\n", name, cases); }
  std::mt19937 g(seed);  // NOLINT(bugprone-random-generator-seed): fixed on purpose, every run checks the same cases
  std::size_t terms = 0;
  for (std::size_t i = 0; i < cases; ++i) {
    const std::size_t failures_before = g_failures;
    const Case k                      = randomCase(g);
    ConcertSide stub;
    writeModel(stub, k);
    stub.finish();
    for (const Row& r : stub.out.rows) {
      terms += r.terms.size();
    }
    DREAMOR_CHECK(compare(k, stub.out));
    stub.env.end();
    if (g_failures != failures_before) {
      std::printf("  %s: case %zu of seed %u failed\n", name, i, seed);
      describe(k);
    }
  }
  std::printf("%s: seed %u, %zu cases, %zu row terms in all\n", name, seed, cases, terms);
  return ::report(name);
}

}  // namespace cross_backend
