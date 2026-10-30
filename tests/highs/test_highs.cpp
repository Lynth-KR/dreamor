// SPDX-License-Identifier: Apache-2.0
// The HiGHS backend (run_tests.sh --highs): VarTable and sumOf as test_vartable and test_sum check
// them for Concert, the expressions and rows of highspy's interface, the objective with its
// Hessian, small models solved, and the code of README.md and highs.hpp's comments as written there.
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "Highs.h"
#include "check.hpp"
#include "dreamor/highs.hpp"
#include "dreamor/index_set.hpp"
#include "float_compare.hpp"

namespace hs = dreamor::highs;

using ICol = dreamor::Col<std::size_t, "i">;
using JCol = dreamor::Col<std::size_t, "j">;

namespace {

struct Row {
  double lower{};
  double upper{};
  std::vector<HighsInt> index;
  std::vector<double> value;
};

Row rowOf(const Highs& highs, HighsInt row) {
  Row out;
  HighsInt count = 0;
  HighsInt nnz   = 0;
  HighsInt start = 0;
  out.index.resize(static_cast<std::size_t>(highs.getNumCol()));
  out.value.resize(static_cast<std::size_t>(highs.getNumCol()));
  highs.getRows(row, row, count, &out.lower, &out.upper, nnz, &start, out.index.data(), out.value.data());
  out.index.resize(static_cast<std::size_t>(nnz));
  out.value.resize(static_cast<std::size_t>(nnz));
  return out;
}

double coefficientOf(const Row& row, hs::HighsVar variable) {
  for (std::size_t k = 0; k < row.index.size(); ++k) {
    if (row.index[k] == variable.index()) { return row.value[k]; }
  }
  return 0.0;
}

std::string colName(const Highs& highs, HighsInt col) {
  std::string name;
  highs.getColName(col, name);
  return name;
}

// Quiet: the Highs is reached, so from here on everything goes to it as written (the incremental path).
Highs& quiet(hs::HighsEnv& env) {
  Highs& highs = env.highs();
  highs.setOptionValue("output_flag", false);
  return highs;
}

}  // namespace

static void varTable() {
  hs::HighsEnv env;
  const Highs& highs = quiet(env);
  const std::vector<std::size_t> I{3, 1, 2};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::makeSet<ICol, JCol>(I, J).collect();

  static_assert(!std::is_copy_constructible_v<hs::VarTable<ICol, JCol>>);
  static_assert(std::is_nothrow_move_constructible_v<hs::VarTable<ICol, JCol>>);
  static_assert(!std::is_default_constructible_v<hs::VarTable<ICol, JCol>>);

  // numbers, integer, names
  const hs::VarTable<ICol, JCol> x{env, IJ, 0, 1, HighsVarType::kInteger, "x"};
  DREAMOR_CHECK(x.size() == 6 && highs.getNumCol() == 6 && x.keys().begin() == IJ.begin());
  DREAMOR_CHECK(x.contains(ICol{1}, JCol{20}) && !x.contains(ICol{9}, JCol{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(ICol{9}, JCol{9}));
  DREAMOR_CHECK_THROWS(std::out_of_range, x(std::tuple{ICol{9}, JCol{9}}));
  DREAMOR_CHECK(colName(highs, x(ICol{1}, JCol{20}).index()) == "x_1_20");
  const auto at = [](hs::HighsVar v) { return static_cast<std::size_t>(v.index()); };
  DREAMOR_CHECK(exactlyEqual(highs.getLp().col_lower_[at(x(ICol{3}, JCol{10}))], 0) &&
                exactlyEqual(highs.getLp().col_upper_[at(x(ICol{3}, JCol{10}))], 1));
  DREAMOR_CHECK(highs.getLp().integrality_[at(x(ICol{3}, JCol{10}))] == HighsVarType::kInteger);
  DREAMOR_CHECK(&x(ICol{3}, JCol{10}).env() == &env);

  // iteration: (row, var) in set order, which is the column order
  std::size_t n = 0;
  for (const auto& [row, var] : x) {
    DREAMOR_CHECK(row == *(IJ.begin() + static_cast<std::ptrdiff_t>(n)));
    DREAMOR_CHECK(var.index() == x(row).index() && std::cmp_equal(var.index(), n));
    ++n;
  }
  DREAMOR_CHECK(n == 6);

  // callable bounds, continuous, and the columns follow on
  const hs::VarTable<ICol, JCol> y{env,
                                   IJ,
                                   [](ICol i) { return -static_cast<double>(i.get()); },
                                   [](ICol i, JCol j) { return i.get() * j.get(); },
                                   HighsVarType::kContinuous,
                                   "y"};
  DREAMOR_CHECK(exactlyEqual(highs.getLp().col_lower_[at(y(ICol{2}, JCol{20}))], -2) &&
                exactlyEqual(highs.getLp().col_upper_[at(y(ICol{2}, JCol{20}))], 40));
  DREAMOR_CHECK(highs.getLp().integrality_[at(y(ICol{2}, JCol{20}))] == HighsVarType::kContinuous);
  DREAMOR_CHECK(y(ICol{3}, JCol{10}).index() == 6);

  // defaults, unnamed
  const hs::VarTable<ICol, JCol> z{env, IJ};
  DREAMOR_CHECK(exactlyEqual(highs.getLp().col_lower_[at(z(ICol{3}, JCol{20}))], 0) &&
                exactlyEqual(highs.getLp().col_upper_[at(z(ICol{3}, JCol{20}))], kHighsInf));
  DREAMOR_CHECK(colName(highs, z(ICol{3}, JCol{20}).index()).empty());

  // HiGHS's other types go through as given
  const hs::VarTable<ICol, JCol> s{env, IJ, 2, 5, HighsVarType::kSemiContinuous};
  DREAMOR_CHECK(highs.getLp().integrality_[at(s(ICol{1}, JCol{10}))] == HighsVarType::kSemiContinuous);

  // an empty set adds no column
  const auto none        = IJ.where([](ICol i) { return i.get() > 100; });
  const HighsInt columns = highs.getNumCol();
  const hs::VarTable<ICol, JCol> e{env, none, 0, 1};
  DREAMOR_CHECK(e.size() == 0 && e.begin() == e.end() && highs.getNumCol() == columns);

  // move keeps lookups working
  hs::VarTable<ICol, JCol> moved{env, IJ, 0, 1, HighsVarType::kContinuous, "w"};
  const auto before = moved(ICol{1}, JCol{10});
  const hs::VarTable<ICol, JCol> moved2{std::move(moved)};
  DREAMOR_CHECK(moved2(ICol{1}, JCol{10}).index() == before.index());
}

static void expressions() {
  hs::HighsEnv env;
  const Highs& highs = quiet(env);
  const std::vector<std::size_t> I{1, 2};
  const hs::VarTable<ICol> v{env, dreamor::makeSet<ICol>(I).collect(), -10, 10};
  const hs::HighsVar x = v(ICol{1});
  const hs::HighsVar y = v(ICol{2});

  // what each operation makes
  static_assert(std::is_same_v<decltype(3 * x), hs::HighsLinearTerm>);
  static_assert(std::is_same_v<decltype(x / 2), hs::HighsLinearTerm>);
  static_assert(std::is_same_v<decltype(-x), hs::HighsLinearTerm>);
  static_assert(std::is_same_v<decltype(x + y), hs::HighsLinearExpression>);
  static_assert(std::is_same_v<decltype(x + 1), hs::HighsLinearExpression>);
  static_assert(std::is_same_v<decltype(x * y), hs::HighsQuadraticTerm>);
  static_assert(std::is_same_v<decltype(2 * x * y), hs::HighsQuadraticTerm>);
  static_assert(std::is_same_v<decltype((x + 1) * (y - 2)), hs::HighsQuadraticExpression>);
  static_assert(std::is_same_v<decltype(x * y + x), hs::HighsQuadraticExpression>);
  static_assert(std::is_same_v<decltype(x + y <= 1), hs::HighsUpperBoundedLinearExpression>);
  static_assert(std::is_same_v<decltype(1 <= x + y), hs::HighsLowerBoundedLinearExpression>);
  static_assert(std::is_same_v<decltype(x <= y), hs::HighsBoundedLinearExpression>);
  static_assert(std::is_same_v<decltype((0 <= x - y) <= 5), hs::HighsBoundedLinearExpression>);
  static_assert(std::is_same_v<decltype(x == 1), hs::HighsBoundedLinearExpression>);

  // an expression keeps its terms as appended
  hs::HighsLinearExpression e = x + x + 2 * y - y + 3;
  DREAMOR_CHECK(e.size() == 4 && exactlyEqual(e.constant(), 3));

  // addConstr merges repeated columns and moves the constant into the bounds
  const hs::HighsCons r0 = hs::addConstr(env, std::move(e) <= 7, "r0");
  const Row row0         = rowOf(highs, r0.index());
  DREAMOR_CHECK(r0.index() == 0 && &r0.env() == &env && row0.index.size() == 2);
  DREAMOR_CHECK(exactlyEqual(coefficientOf(row0, x), 2) && exactlyEqual(coefficientOf(row0, y), 1));
  DREAMOR_CHECK(exactlyEqual(row0.upper, 4) && exactlyEqual(row0.lower, -kHighsInf));
  std::string name;
  highs.getRowName(0, name);
  DREAMOR_CHECK(name == "r0");

  // a column whose terms cancel is left out
  const Row row1 =
      rowOf(highs, hs::addConstr(env, x - x + y >= 1).index());  // NOLINT(misc-redundant-expression): x - x is the case
  DREAMOR_CHECK(row1.index.size() == 1 && exactlyEqual(coefficientOf(row1, y), 1) && exactlyEqual(row1.lower, 1) &&
                exactlyEqual(row1.upper, kHighsInf));

  // ranges, either way round
  const Row row2 = rowOf(highs, hs::addConstr(env, (0 <= x - y) <= 5).index());
  DREAMOR_CHECK(exactlyEqual(row2.lower, 0) && exactlyEqual(row2.upper, 5) && exactlyEqual(coefficientOf(row2, y), -1));
  const Row row3 = rowOf(highs, hs::addConstr(env, (5 >= x + y) >= 1).index());
  DREAMOR_CHECK(exactlyEqual(row3.lower, 1) && exactlyEqual(row3.upper, 5));

  // two expressions, and equality
  const Row row4 = rowOf(highs, hs::addConstr(env, 2 * x == y + 1).index());
  DREAMOR_CHECK(exactlyEqual(row4.lower, 1) && exactlyEqual(row4.upper, 1));
  DREAMOR_CHECK(exactlyEqual(coefficientOf(row4, x), 2) && exactlyEqual(coefficientOf(row4, y), -1));
  const Row row5 = rowOf(highs, hs::addConstr(env, x + 4 >= y).index());
  DREAMOR_CHECK(exactlyEqual(row5.lower, -4) && exactlyEqual(row5.upper, kHighsInf));
  DREAMOR_CHECK(highs.getNumRow() == 6);

  // an expression added to or taken from itself
  // (terms are appended, as for any other operand; the model merges them)
  hs::HighsLinearExpression twice = x + 1;
  twice += twice;  // NOLINT(misc-redundant-expression): adding an expression to itself is the case
  DREAMOR_CHECK(twice.size() == 2 && exactlyEqual(twice.coefficients()[1], 1) && exactlyEqual(twice.constant(), 2));
  hs::HighsLinearExpression gone = x + 1;
  gone -= gone;  // NOLINT(misc-redundant-expression): taking an expression from itself is the case
  DREAMOR_CHECK(gone.size() == 2 && exactlyEqual(gone.coefficients()[1], -1) && isExactlyZero(gone.constant()));
  hs::HighsQuadraticExpression squares = x * x + y;
  squares += squares;  // NOLINT(misc-redundant-expression): adding an expression to itself is the case
  DREAMOR_CHECK(squares.first().size() == 2 && squares.linear().size() == 2);

  // a product of expressions, expanded: (x + 1)(y - 2) = xy - 2x + y - 2
  const hs::HighsQuadraticExpression product = (x + 1) * (y - 2);
  DREAMOR_CHECK(product.first().size() == 1 && exactlyEqual(product.quadraticCoefficients()[0], 1));
  DREAMOR_CHECK(exactlyEqual(product.linear().constant(), -2));
  double xCoef = 0;
  double yCoef = 0;
  for (std::size_t k = 0; k < product.linear().size(); ++k) {
    (product.linear().indices()[k] == x.index() ? xCoef : yCoef) += product.linear().coefficients()[k];
  }
  DREAMOR_CHECK(exactlyEqual(xCoef, -2) && exactlyEqual(yCoef, 1));
  // a quadratic term is stored in the lower triangle
  const hs::HighsQuadraticExpression cross = x * y + y * x;
  DREAMOR_CHECK(cross.first()[0] == y.index() && cross.second()[0] == x.index() && cross.first()[1] == y.index());
}

static void objective() {
  // min (x - y)^2 + (x - 2)^2 + y^2  s.t.  x + y >= 1:  x = 4/3, y = 2/3, objective 4/3.
  hs::HighsEnv env;
  const Highs& highs = quiet(env);
  const std::vector<std::size_t> I{1, 2};
  const hs::VarTable<ICol> v{env, dreamor::makeSet<ICol>(I).collect(), -10, 10};
  const hs::HighsVar x = v(ICol{1});
  const hs::HighsVar y = v(ICol{2});
  hs::addConstr(env, x + y >= 1);

  DREAMOR_CHECK(hs::minimize(env, (x - y) * (x - y) + (x - 2) * (x - 2) + y * y) == HighsStatus::kOk);
  DREAMOR_CHECK(highs.getModelStatus() == HighsModelStatus::kOptimal);
  DREAMOR_CHECK(std::abs(highs.getInfo().objective_function_value - (4.0 / 3.0)) < 1e-6);
  const auto plan = v.solution(env);
  DREAMOR_CHECK(std::abs(plan(ICol{1}) - (4.0 / 3.0)) < 1e-6 && std::abs(plan(ICol{2}) - (2.0 / 3.0)) < 1e-6);
  DREAMOR_CHECK(highs.getModel().hessian_.dim_ == 2);

  // the same objective from repeated, unordered terms: x*y and y*x merge into one entry
  DREAMOR_CHECK(hs::minimize(env, x * x - x * y - y * x + y * y + x * x - 4 * x + 4 + y * y) == HighsStatus::kOk);
  DREAMOR_CHECK(std::abs(highs.getInfo().objective_function_value - (4.0 / 3.0)) < 1e-6);
  DREAMOR_CHECK(highs.getModel().hessian_.numNz() == 3);

  // a linear objective clears the Hessian, and resets every cost it does not name
  DREAMOR_CHECK(hs::minimize(env, x + y) == HighsStatus::kOk);
  DREAMOR_CHECK(highs.getModel().hessian_.dim_ == 0);
  DREAMOR_CHECK(std::abs(highs.getInfo().objective_function_value - 1) < 1e-6);
  hs::setObjective(env, 2 * x);
  DREAMOR_CHECK(exactlyEqual(highs.getLp().col_cost_[0], 2) && isExactlyZero(highs.getLp().col_cost_[1]));
  DREAMOR_CHECK(highs.getLp().sense_ == ObjSense::kMinimize);  // no sense given: unchanged

  DREAMOR_CHECK(hs::maximize(env, x - 1) == HighsStatus::kOk);
  DREAMOR_CHECK(std::abs(highs.getInfo().objective_function_value - 9) < 1e-6 &&
                highs.getLp().sense_ == ObjSense::kMaximize);

  // a single variable, a term and a number are objectives too
  hs::setObjective(env, y, ObjSense::kMinimize);
  DREAMOR_CHECK(exactlyEqual(highs.getLp().col_cost_[1], 1) && isExactlyZero(highs.getLp().col_cost_[0]));
  hs::setObjective(env, 5);
  DREAMOR_CHECK(isExactlyZero(highs.getLp().col_cost_[1]) && exactlyEqual(highs.getLp().offset_, 5));
}

static void sum() {
  hs::HighsEnv env;  // sums alone: the Highs is never reached
  const std::vector<std::size_t> I{1, 2, 3};
  const std::vector<std::size_t> J{10, 20};
  const auto IJ = dreamor::makeSet<ICol, JCol>(I, J).collect();
  const hs::VarTable<ICol, JCol> x{env, IJ, 0, 1, HighsVarType::kInteger, "x"};
  const auto var      = [&x](ICol i, JCol j) { return x(i, j); };
  const auto weighted = [&x](ICol i, JCol j) { return static_cast<double>(i.get()) * x(i, j); };

  DREAMOR_CHECK(hs::sumOf(IJ, var).build().size() == 6);
  DREAMOR_CHECK(hs::sumOf(IJ, var).where([](ICol i) { return i.get() > 1; }).build().size() == 4);

  const auto byJ = IJ.groupBy<JCol>();
  for (const auto& [j, rows] : byJ) {
    DREAMOR_CHECK(hs::sumOf(rows, var).build().size() == 3);
  }

  const auto sums =
      hs::sumOf(IJ, weighted).split([](JCol j) { return j.get() == 10; }, [](JCol) { return true; }).build();
  DREAMOR_CHECK(sums[0].size() == 3 && sums[1].size() == 6);
  double coef = 0;
  for (const double c : sums[0].coefficients()) {
    coef += c;
  }
  DREAMOR_CHECK(exactlyEqual(coef, 1 + 2 + 3));

  // a table is itself a term
  DREAMOR_CHECK(hs::sumOf(IJ, x).build().size() == 6);
  DREAMOR_CHECK(hs::sumOf(IJ, x).where([](ICol i) { return i.get() > 1; }).build().size() == 4);

  // an empty sum is the zero expression
  const auto empty = hs::sumOf(IJ, var).where([](ICol) { return false; }).build();
  DREAMOR_CHECK(empty.size() == 0 && isExactlyZero(empty.constant()));
  // no term on either side, so no model: the product is the constants' alone
  const hs::HighsQuadraticExpression constants = (empty + 2) * (empty + 3);
  DREAMOR_CHECK(constants.first().empty() && constants.linear().size() == 0);
  DREAMOR_CHECK(exactlyEqual(constants.linear().constant(), 6));

  // a plain container as a one-column set
  const hs::VarTable<ICol> y{env, dreamor::makeSet<ICol>(I).collect(), 0, 1};
  DREAMOR_CHECK(hs::sumOf<ICol>(I, [&y](ICol i) { return y(i); }).build().size() == 3);

  // linear terms make a HighsLinearExpression, quadratic ones a HighsQuadraticExpression
  static_assert(std::is_same_v<decltype(hs::sumOf(IJ, weighted).build()), hs::HighsLinearExpression>);
  const auto square    = [&x](ICol i, JCol j) { return static_cast<double>(i.get()) * x(i, j) * x(i, j); };
  const auto quadratic = hs::sumOf(IJ, square).build();
  static_assert(std::is_same_v<std::remove_const_t<decltype(quadratic)>, hs::HighsQuadraticExpression>);
  DREAMOR_CHECK(quadratic.first().size() == 6 && quadratic.linear().size() == 0);
  double squareCoef = 0;
  for (const double c : quadratic.quadraticCoefficients()) {
    squareCoef += c;
  }
  DREAMOR_CHECK(exactlyEqual(squareCoef, 2 * (1 + 2 + 3)));

  // a quadratic expression term, under where() and split(); (x - 1)^2 appends -x twice, unmerged
  const auto deviation = [&x](ICol i, JCol j) { return (x(i, j) - 1) * (x(i, j) - 1); };
  const auto parts     = hs::sumOf(IJ, deviation)
                             .where([](ICol i) { return i.get() > 1; })
                             .split([](JCol j) { return j.get() == 10; }, [](JCol) { return true; })
                             .build();
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(parts[0])>, hs::HighsQuadraticExpression>);
  DREAMOR_CHECK(parts[0].first().size() == 2 && parts[1].first().size() == 4);
  DREAMOR_CHECK(parts[1].linear().size() == 8 && exactlyEqual(parts[1].linear().constant(), 4));
}

static void solution() {
  // max sum i * x_i, sum x_i <= 2, x binary over I = {1, 2, 3}: x_2 = x_3 = 1.
  hs::HighsEnv env;
  Highs& highs = quiet(env);
  const std::vector<std::size_t> I{1, 2, 3};
  const auto sI = dreamor::makeSet<ICol>(I).collect();
  const hs::VarTable<ICol> vPick{env, sI, 0, 1, HighsVarType::kInteger, "Pick"};
  DREAMOR_CHECK_THROWS(std::logic_error, vPick.solution(env));  // not run yet

  hs::addConstr(env, hs::sumOf(sI, vPick).build() <= 2);
  const auto value = [&vPick](ICol i) { return static_cast<double>(i.get()) * vPick(i); };
  DREAMOR_CHECK(hs::maximize(env, hs::sumOf(sI, value).build()) == HighsStatus::kOk);
  const auto pickPlan = vPick.solution(env);
  DREAMOR_CHECK(pickPlan.size() == 3 && std::abs(pickPlan(ICol{1})) < 1e-6 && std::abs(pickPlan(ICol{3}) - 1) < 1e-6);
  DREAMOR_CHECK_THROWS(std::out_of_range, pickPlan(ICol{9}));

  // no primal solution: a clear exception instead of a stale col_value
  hs::addConstr(env, vPick(ICol{1}) >= 2);
  highs.run();
  DREAMOR_CHECK(highs.getModelStatus() == HighsModelStatus::kInfeasible);
  DREAMOR_CHECK_THROWS(std::logic_error, vPick.solution(env));
}

static void collected() {
  // What is written before the Highs is reached is collected and reaches it in one passModel: the
  // columns, the merged rows, the names given (and the empty ones), the objective with its Hessian.
  hs::HighsEnv env;
  const std::vector<std::size_t> I{1, 2};
  const auto sI = dreamor::makeSet<ICol>(I).collect();
  const hs::VarTable<ICol> x{env, sI, 0, 10, HighsVarType::kContinuous, "x"};
  const hs::VarTable<ICol> u{env, sI, -1, 1};
  const hs::HighsVar x1 = x(ICol{1});
  const hs::HighsVar x2 = x(ICol{2});
  const hs::HighsVar u1 = u(ICol{1});
  const hs::HighsVar u2 = u(ICol{2});
  DREAMOR_CHECK(hs::addConstr(env, x1 + x1 + u2 <= 7, "twice").index() == 0);
  DREAMOR_CHECK(hs::addConstr(env, (1 <= x2 - u1) <= 3).index() == 1);
  hs::setObjective(env, (x1 - 1) * (x1 - 1) + u2, ObjSense::kMinimize);
  const hs::VarTable<ICol> late{env, sI, 0, 1};  // after the objective: no cost, no square

  const Highs& highs = env.highs();  // the model reaches the Highs here
  const HighsLp& lp  = highs.getLp();
  DREAMOR_CHECK(lp.num_col_ == 6 && lp.num_row_ == 2);
  DREAMOR_CHECK(colName(highs, x1.index()) == "x_1" && colName(highs, u1.index()).empty());
  DREAMOR_CHECK(exactlyEqual(lp.col_lower_[2], -1) && exactlyEqual(lp.col_upper_[2], 1));
  const Row twice = rowOf(highs, 0);
  DREAMOR_CHECK(twice.index.size() == 2 && exactlyEqual(coefficientOf(twice, x1), 2) && exactlyEqual(twice.upper, 7));
  const Row range = rowOf(highs, 1);
  DREAMOR_CHECK(exactlyEqual(range.lower, 1) && exactlyEqual(range.upper, 3) &&
                exactlyEqual(coefficientOf(range, u1), -1));
  std::string name;
  highs.getRowName(0, name);
  DREAMOR_CHECK(name == "twice");
  // (x1 - 1)^2 + u2 = x1^2 - 2 x1 + u2 + 1; the Hessian reaches the late columns, empty
  DREAMOR_CHECK(exactlyEqual(lp.col_cost_[0], -2) && exactlyEqual(lp.col_cost_[3], 1) &&
                isExactlyZero(lp.col_cost_[4]));
  // (HiGHS fills in the diagonal of a triangular Hessian it is passed, with zeros: x1's entry is the one set)
  const HighsHessian& hessian = highs.getModel().hessian_;
  DREAMOR_CHECK(exactlyEqual(lp.offset_, 1) && hessian.dim_ == 6 && exactlyEqual(hessian.value_[0], 2));
  double others = 0;
  for (std::size_t k = 1; k < hessian.value_.size(); ++k) {
    others += std::abs(hessian.value_[k]);
  }
  DREAMOR_CHECK(isExactlyZero(others));

  // from here on, what is written goes to the Highs as it is written
  hs::addConstr(env, late(ICol{1}) >= 1, "after");
  DREAMOR_CHECK(highs.getNumRow() == 3);
  Highs& solver = env.highs();
  solver.setOptionValue("output_flag", false);
  DREAMOR_CHECK(solver.run() == HighsStatus::kOk && solver.getModelStatus() == HighsModelStatus::kOptimal);
  const auto plan = x.solution(env);
  DREAMOR_CHECK(std::abs(plan(ICol{1}) - 1) < 1e-6 && std::abs(late.solution(env)(ICol{1}) - 1) < 1e-6);

  // a model with no name at all passes none; the column types go as given
  hs::HighsEnv unnamed;
  const hs::VarTable<ICol> v{unnamed, sI, 0, 3, HighsVarType::kInteger};
  const hs::VarTable<ICol> c{unnamed, sI};
  hs::addConstr(unnamed, v(ICol{1}) + c(ICol{2}) <= 1);
  const HighsLp& unnamedLp = unnamed.highs().getLp();
  DREAMOR_CHECK(unnamedLp.col_names_.empty() && unnamedLp.row_names_.empty());
  DREAMOR_CHECK(unnamedLp.integrality_[0] == HighsVarType::kInteger &&
                unnamedLp.integrality_[2] == HighsVarType::kContinuous);

  // options given to the environment are the Highs's before anything reaches it
  HighsOptions options;
  options.output_flag = false;
  options.time_limit  = 7;
  hs::HighsEnv configured{options};
  const hs::VarTable<ICol> t{configured, sI, 0, 1};
  hs::addConstr(configured, t(ICol{1}) + t(ICol{2}) >= 1);
  DREAMOR_CHECK(hs::minimize(configured, t(ICol{1}) + 2 * t(ICol{2})) == HighsStatus::kOk);
  DREAMOR_CHECK(exactlyEqual(configured.highs().getOptions().time_limit, 7) &&
                !configured.highs().getOptions().output_flag);
  DREAMOR_CHECK(std::abs(configured.highs().getInfo().objective_function_value - 1) < 1e-9);

  // a Highs reached before anything is written passes nothing, and the model goes to it as written
  hs::HighsEnv early;
  early.highs().setOptionValue("output_flag", false);
  const hs::VarTable<ICol> w{early, sI, 0, 1, HighsVarType::kContinuous, "w"};
  DREAMOR_CHECK(early.highs().getNumCol() == 2 && colName(early.highs(), 1) == "w_2");
}

static void errors() {
  // A HiGHS call that fails throws HighsError with its status: once the Highs is reached, at the call.
  hs::HighsEnv env;
  quiet(env);
  const std::vector<std::size_t> I{1};
  const auto sI = dreamor::makeSet<ICol>(I).collect();
  bool thrown   = false;
  try {
    const hs::VarTable<ICol> bad{env, sI, std::numeric_limits<double>::quiet_NaN(), 1};
  } catch (const hs::HighsError& error) {
    thrown = error.status() == HighsStatus::kError && std::string{error.call()} == "addVars";
  }
  DREAMOR_CHECK(thrown);

  // collected, the same column reaches HiGHS when the Highs is reached: the error is passModel's
  hs::HighsEnv collecting;
  const hs::VarTable<ICol> later{collecting, sI, std::numeric_limits<double>::quiet_NaN(), 1};
  bool passed = false;
  try {
    static_cast<void>(collecting.highs());
  } catch (const hs::HighsError& error) {
    passed = error.status() == HighsStatus::kError && std::string{error.call()} == "passModel";
  }
  DREAMOR_CHECK(passed);
}

namespace {
namespace lot {

struct Item {
  std::string name;
  double time;
  double cost;
};
struct Machine {
  std::string name;
  double capacity;
  std::vector<const Item*> items;
};
inline const std::string& keyName(const Item* i) {
  return i->name;
}
inline const std::string& keyName(const Machine* m) {
  return m->name;
}

}  // namespace lot
}  // namespace

using MachineCol = dreamor::Col<const lot::Machine*, "machine">;
using ItemCol    = dreamor::Col<const lot::Item*, "item">;
using PeriodCol  = dreamor::Col<std::size_t, "period">;

// README.md (Backends) and highs.hpp's comments, as written there, on one machine making two items in
// two periods. Only the lines marked "test only" are not theirs.
static void docs() {
  const lot::Item a{"A", 1.0, 2.0};
  const lot::Item b{"B", 2.0, 1.0};
  const lot::Machine m1{"M1", 10.0, {&a, &b}};
  const std::vector<const lot::Machine*> machines{&m1};
  const std::vector<std::size_t> periods{1, 2};
  const auto items_of         = [](MachineCol m) -> const std::vector<const lot::Item*>& { return m->items; };
  const auto sPRODUCE         = dreamor::makeSet<MachineCol, ItemCol, PeriodCol>(machines, items_of, periods).collect();
  const auto max_possible_qty = [](MachineCol m, ItemCol i) { return m->capacity / i->time; };
  const auto producedByMachinePeriod = sPRODUCE.groupBy<MachineCol, PeriodCol>();

  {  // README.md, Backends
    hs::HighsEnv env;
    const hs::VarTable<MachineCol, ItemCol, PeriodCol> vYield{
        env, sPRODUCE, 0, max_possible_qty, HighsVarType::kContinuous, "Yield"};
    const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };

    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      hs::addConstr(env, hs::sumOf(rows, machine_usage).build() <= machine->capacity);
    }

    quiet(env);  // test only
    hs::minimize(env, hs::sumOf(sPRODUCE, vYield).build());
    const auto yieldPlan = vYield.solution(env);
    DREAMOR_CHECK(env.highs().getModelStatus() == HighsModelStatus::kOptimal && yieldPlan.size() == sPRODUCE.size());
    DREAMOR_CHECK(env.highs().getNumRow() == 2 && colName(env.highs(), 0) == "Yield_M1_A_1");
  }
  {  // highs.hpp, at the top
    hs::HighsEnv env;
    const hs::VarTable<MachineCol, ItemCol, PeriodCol> vYield{
        env, sPRODUCE, 0, max_possible_qty, HighsVarType::kContinuous, "Yield"};
    const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };
    const auto unit_cost     = [&](MachineCol m, ItemCol i, PeriodCol t) { return -i->cost * vYield(m, i, t); };
    for (const auto& [machine, period, rows] : producedByMachinePeriod) {
      const hs::HighsLinearExpression totalLoad = hs::sumOf(rows, machine_usage).build();
      hs::addConstr(env, totalLoad <= machine->capacity);
    }

    quiet(env);                                                 // test only
    hs::minimize(env, hs::sumOf(sPRODUCE, unit_cost).build());  // sets the objective, then run()
    const auto yieldPlan = vYield.solution(env);
    // the most valuable per unit of time: A, 2 a unit at 1 hour, fills each period's 10 hours
    DREAMOR_CHECK(exactlyEqual(yieldPlan(MachineCol{&m1}, ItemCol{&a}, PeriodCol{1}), 10.0));
    DREAMOR_CHECK(isExactlyZero(yieldPlan(MachineCol{&m1}, ItemCol{&b}, PeriodCol{2})));
  }
  {  // highs.hpp, HighsEnv
    const auto& rows      = sPRODUCE;
    const double capacity = m1.capacity * 2;
    HighsOptions options;
    options.output_flag = false;
    options.time_limit  = 60;
    hs::HighsEnv env{options};
    const hs::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty};
    const auto machine_usage = [&](MachineCol m, ItemCol i, PeriodCol t) { return i->time * vYield(m, i, t); };
    const auto unit_cost     = [&](MachineCol m, ItemCol i, PeriodCol t) { return -i->cost * vYield(m, i, t); };
    hs::addConstr(env, hs::sumOf(rows, machine_usage).build() <= capacity);
    hs::minimize(env, hs::sumOf(sPRODUCE, unit_cost).build());  // the model reaches the Highs here
    DREAMOR_CHECK(env.highs().getModelStatus() == HighsModelStatus::kOptimal);
    DREAMOR_CHECK(std::abs(env.highs().getInfo().objective_function_value + 40.0) < 1e-9);
  }
}

int main() try {
  varTable();
  expressions();
  objective();
  sum();
  solution();
  collected();
  errors();
  docs();
  return report("highs");
} catch (...) { return ::uncaught(); }
