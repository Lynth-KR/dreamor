// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The HiGHS backend's model: VarTable, addConstr, setObjective, minimize, maximize, val and sumExpr.
#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/index_set.hpp"
#include "dreamor/internal/backend/row_index.hpp"
#include "dreamor/internal/backend/sum_builder.hpp"
#include "dreamor/internal/backend/var_table_core.hpp"
#include "dreamor/internal/highs/environment.hpp"
#include "dreamor/internal/highs/error.hpp"
#include "dreamor/internal/highs/expression.hpp"
#include "dreamor/internal/highs/operators.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/logger.hpp"
#include "dreamor/param_table.hpp"
#include "dreamor/solution_table.hpp"

#include "dreamor/internal/base/third_party_headers.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#include "Highs.h"
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::highs::detail {

/// @contract highs_finite_numbers: Every coefficient of a row that `addConstr` receives is finite, and neither of
/// its bounds is NaN. Every coefficient and the constant of an objective that `setObjective`, `minimize` or
/// `maximize` receives are finite.
///
/// A bound of `-kHighsInf` or `kHighsInf` means that the row has no bound on that side. A division by zero breaks
/// the contract: `x / 0` has an infinite coefficient, and `0 / 0` is NaN. HiGHS rejects some of these values when the
/// model is handed over, and takes the others without an error: it drops a NaN coefficient of a row, and solves with
/// an infinite or a NaN cost.

// Checks highs_finite_numbers for a row.
inline void checkHighsRow([[maybe_unused]] const HighsLinearExpression& expression,
                          [[maybe_unused]] double lower,
                          [[maybe_unused]] double upper) {
#ifndef NDEBUG
  if (!std::ranges::all_of(expression.coefficients(), [](double c) { return std::isfinite(c); })) {
    DREAMOR_DEBUG_FATAL(
        "dreamor::highs::addConstr: a coefficient of the row is not finite - a broken contract (dreamor/highs.hpp).");
  }
  if (std::isnan(lower) || std::isnan(upper)) {
    DREAMOR_DEBUG_FATAL(
        "dreamor::highs::addConstr: a bound of the row is NaN - a broken contract (dreamor/highs.hpp).");
  }
#endif
}

// Checks highs_finite_numbers for an objective, linear and quadratic.
inline void checkHighsObjective([[maybe_unused]] const HighsLinearExpression& linear,
                                [[maybe_unused]] const HighsQuadraticExpression* squares) {
#ifndef NDEBUG
  const auto finite = [](double value) { return std::isfinite(value); };
  if (!std::isfinite(linear.constant()) || !std::ranges::all_of(linear.coefficients(), finite) ||
      (squares != nullptr && !std::ranges::all_of(squares->quadraticCoefficients(), finite))) {
    DREAMOR_DEBUG_FATAL(
        "dreamor::highs::setObjective: a coefficient or the constant of the objective is not finite - a broken "
        "contract (dreamor/highs.hpp).");
  }
#endif
}

// The functions with which VarTable, addConstr and setObjective write the model. Before the model is handed over,
// they write into the collected model. After that, they write into the Highs.
struct HighsEnvAccess {
  using HighsEnv = dreamor::highs::HighsEnv;

  [[nodiscard]] static HighsInt numCol(const HighsEnv& env) noexcept {
    return env.passed_ ? env.highs_.getNumCol() : static_cast<HighsInt>(env.model_.lp_.col_cost_.size());
  }

  // Adds `count` columns of one type and returns the index of the first. bounds(lower, upper) writes their bounds
  // directly into the arrays of the collected model. After the model is handed over, bounds() writes into arrays
  // for one addVars call instead.
  template <typename Bounds>
  static HighsInt addColumns(HighsEnv& env, std::size_t count, HighsVarType type, const Bounds& bounds) {
    const HighsInt first = numCol(env);
    if (env.passed_) {
      std::vector<double> lower(count);
      std::vector<double> upper(count);
      bounds(std::span<double>{lower}, std::span<double>{upper});
      highsCall("addVars", env.highs_.addVars(highsCount(count), lower.data(), upper.data()));
      if (type != HighsVarType::kContinuous) {
        const std::vector<HighsVarType> types(count, type);
        const HighsInt last = highsCount(static_cast<std::size_t>(first) + count) - 1;
        highsCall("changeColsIntegrality", env.highs_.changeColsIntegrality(first, last, types.data()));
      }
      return first;
    }
    HighsLp& lp             = env.model_.lp_;
    const std::size_t begin = lp.col_lower_.size();
    lp.col_lower_.resize(begin + count);
    lp.col_upper_.resize(begin + count);
    bounds(std::span<double>{lp.col_lower_}.subspan(begin), std::span<double>{lp.col_upper_}.subspan(begin));
    lp.col_cost_.resize(begin + count, 0.0);
    // HiGHS reads an empty integrality array as all continuous, and warns about an array that is not empty but has
    // no integer column. So the array is filled only when a column is not continuous, and the columns before that
    // column are set to continuous.
    if (type != HighsVarType::kContinuous || !lp.integrality_.empty()) {
      lp.integrality_.resize(begin, HighsVarType::kContinuous);
      lp.integrality_.resize(begin + count, type);
    }
    return first;
  }

  static void nameColumn(HighsEnv& env, HighsInt column, std::string_view name) {
    if (env.passed_) {
      highsCall("passColName", env.highs_.passColName(column, std::string{name}));
      return;
    }
    std::vector<std::string>& names = env.model_.lp_.col_names_;
    if (names.size() <= static_cast<std::size_t>(column)) { names.resize(env.model_.lp_.col_cost_.size()); }
    names[static_cast<std::size_t>(column)] = name;
    env.named_                              = true;
  }

  // Adds a row whose terms over one column are merged, and returns the index of the row. Before the model is handed
  // over, the merged terms go directly into the row-wise matrix of the collected model.
  static HighsInt addRow(HighsEnv& env,
                         double lower,
                         double upper,
                         std::span<const HighsInt> indices,
                         std::span<const double> coefficients) {
    HighsWorkspace& workspace = HighsWorkspace::local();
    const auto columns        = static_cast<std::size_t>(numCol(env));
    if (env.passed_) {
      workspace.merge(indices, coefficients, columns);
      const std::span<const HighsInt> merged = workspace.columns();
      highsCall("addRow",
                env.highs_.addRow(lower, upper, highsCount(merged.size()), merged.data(), workspace.values().data()));
      return env.highs_.getNumRow() - 1;
    }
    HighsLp& lp             = env.model_.lp_;
    HighsSparseMatrix& rows = lp.a_matrix_;
    workspace.appendMerged(indices, coefficients, columns, rows.index_, rows.value_);
    rows.start_.push_back(highsCount(rows.index_.size()));
    lp.row_lower_.push_back(lower);
    lp.row_upper_.push_back(upper);
    return highsCount(lp.row_lower_.size()) - 1;
  }

  static void nameRow(HighsEnv& env, HighsInt row, std::string_view name) {
    if (env.passed_) {
      highsCall("passRowName", env.highs_.passRowName(row, std::string{name}));
      return;
    }
    std::vector<std::string>& names = env.model_.lp_.row_names_;
    if (names.size() <= static_cast<std::size_t>(row)) { names.resize(env.model_.lp_.row_lower_.size()); }
    names[static_cast<std::size_t>(row)] = name;
    env.named_                           = true;
  }

  // Sets the cost of every column, which is zero for a column without a term, the constant and the Hessian. The
  // Hessian is built from `squares`, or cleared if `squares` is null.
  static void setObjective(HighsEnv& env,
                           const dreamor::highs::HighsLinearExpression& linear,
                           const dreamor::highs::HighsQuadraticExpression* squares) {
    checkHighsModel(HighsModelAccess::of(linear), env, "setObjective");
    checkHighsObjective(linear, squares);
    const auto columns        = static_cast<std::size_t>(numCol(env));
    HighsWorkspace& workspace = HighsWorkspace::local();
    if (!env.passed_) {
      HighsModel& model = env.model_;
      if (columns > 0) { HighsWorkspace::fillCosts(linear, columns, model.lp_.col_cost_); }
      model.lp_.offset_ = linear.constant();
      model.hessian_    = HighsHessian{};
      if (squares != nullptr) {
        workspace.hessian(*squares, columns);
        model.hessian_.dim_    = highsCount(columns);
        model.hessian_.format_ = HessianFormat::kTriangular;
        model.hessian_.start_.assign(workspace.hessianStart().begin(), workspace.hessianStart().end());
        model.hessian_.index_.assign(workspace.hessianIndex().begin(), workspace.hessianIndex().end());
        model.hessian_.value_.assign(workspace.hessianValue().begin(), workspace.hessianValue().end());
      }
      return;
    }
    Highs& highs = env.highs_;
    if (columns > 0) {
      const std::span<const double> costs = workspace.costs(linear, columns);
      highsCall("changeColsCost", highs.changeColsCost(0, highs.getNumCol() - 1, costs.data()));
    }
    highsCall("changeObjectiveOffset", highs.changeObjectiveOffset(linear.constant()));
    constexpr auto kLowerTriangle = static_cast<HighsInt>(HessianFormat::kTriangular);
    if (squares != nullptr) {
      workspace.hessian(*squares, columns);
      highsCall("passHessian",
                highs.passHessian(highs.getNumCol(),
                                  highsCount(workspace.hessianIndex().size()),
                                  kLowerTriangle,
                                  workspace.hessianStart().data(),
                                  workspace.hessianIndex().data(),
                                  workspace.hessianValue().data()));
    } else if (highs.getModel().hessian_.dim_ > 0) {
      highsCall(
          "passHessian",
          highs.passHessian(
              highs.getNumCol(), 0, kLowerTriangle, workspace.emptyHessianStart(columns).data(), nullptr, nullptr));
    }
  }

  static void setSense(HighsEnv& env, ObjSense sense) {
    if (env.passed_) {
      highsCall("changeObjectiveSense", env.highs_.changeObjectiveSense(sense));
      return;
    }
    env.model_.lp_.sense_ = sense;
  }
};

// Checks whether the last solve left a feasible primal solution to read. After HiGHS finds the model infeasible, it
// keeps value_valid set and holds the infeasible point at which it stopped, so value_valid alone would return that
// point as a solution.
[[nodiscard]] inline bool hasPrimalSolution(const HighsEnv& env) {
  return env.highs().getSolution().value_valid &&
         env.highs().getInfo().primal_solution_status == SolutionStatus::kSolutionStatusFeasible;
}

// The value of the constant and the linear terms of `expression` at the columns' values `x`.
[[nodiscard]] inline double linearValue(const std::vector<double>& x, const HighsLinearExpression& expression) {
  double value                               = expression.constant();
  const std::span<const HighsInt> indices    = expression.indices();
  const std::span<const double> coefficients = expression.coefficients();
  for (std::size_t k = 0; k < indices.size(); ++k) {
    value += coefficients[k] * x[static_cast<std::size_t>(indices[k])];
  }
  return value;
}

// The variables of a HiGHS table. The columns are added in one call, so the variable of a row is the first column
// plus the position of the row. Nothing is stored for each variable, where a vector of HighsVar would store 16 bytes
// for each row.
class HighsColumns {
 public:
  HighsColumns() = default;

  HighsColumns(HighsInt first, const dreamor::highs::HighsEnv& env, std::size_t count) noexcept
      : first_{first}, env_{&env}, count_{count} {}

  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  [[nodiscard]] dreamor::highs::HighsVar operator[](std::size_t at) const noexcept {
    return {first_ + static_cast<HighsInt>(at), *env_};
  }

  // A position among the columns, which moves forward as a pointer does.
  class Cursor {
   public:
    Cursor() = default;

    Cursor(HighsInt column, const dreamor::highs::HighsEnv* env) noexcept : column_{column}, env_{env} {}

    [[nodiscard]] dreamor::highs::HighsVar operator*() const noexcept { return {column_, *env_}; }

    Cursor& operator++() noexcept {
      ++column_;
      return *this;
    }

   private:
    HighsInt column_{0};
    const dreamor::highs::HighsEnv* env_{nullptr};
  };

  [[nodiscard]] friend Cursor cursorAt(const HighsColumns& columns, std::size_t at) noexcept {
    return {columns.first_ + static_cast<HighsInt>(at), columns.env_};
  }

 private:
  HighsInt first_{0};
  const dreamor::highs::HighsEnv* env_{nullptr};
  std::size_t count_{0};
};

}  // namespace dreamor::highs::detail

template <>
struct dreamor::internal::VarStorage<dreamor::highs::HighsVar> {
  using type = dreamor::highs::detail::HighsColumns;
};

namespace dreamor::highs {

/// @section dreamor::highs

inline HighsVar HighsEnv::addOneVariable(double lb, double ub, HighsVarType type, std::string_view name) {
  const auto bounds = [lb, ub](std::span<double> lower, std::span<double> upper) {
    lower[0] = lb;
    upper[0] = ub;
  };
  const HighsInt column = detail::HighsEnvAccess::addColumns(*this, 1, type, bounds);
  if (!name.empty()) { detail::HighsEnvAccess::nameColumn(*this, column, name); }
  return HighsVar{column, *this};
}

/// A family of HiGHS variables over a set, one variable for each row of the set.
///
/// The variables are columns of the model in a `HighsEnv`. The other members of the table are those of
/// `VarTableCore`.
///
/// Takes: `Keys` must be one or more distinct columns whose values can be written in a name.
/// Cost: The columns of a table are consecutive. The table therefore stores only the index of the first column, and
/// no data for each variable.
template <typename... Keys>
  requires(sizeof...(Keys) > 0 && (internal::nameable<Keys> && ...))
class VarTable : public internal::VarTableCore<HighsVar, Keys...> {
  using Core = internal::VarTableCore<HighsVar, Keys...>;

 public:
  using key_type = Core::key_type;
  using Core::Core;

  /// @anchor vartable-constructor-empty
  /// Constructs a table on the empty set, with no variables. A family is moved into it once, by assignment (the
  /// VarTable section).
  ///
  /// Cost: No memory is allocated.
  VarTable() noexcept = default;

  /// @hidden - exists only to reject a name in the place of the type with a message
  template <typename Lb, typename Ub, typename Name>
    requires std::is_convertible_v<const Name&, std::string_view>
  VarTable(HighsEnv& /*env*/, const IndexSet<Keys...>& set, const Lb& /*lb*/, const Ub& /*ub*/, const Name& /*name*/)
      : Core{set} {
    static_assert(internal::always_false<Name>,
                  "A VarTable takes the variables' type before their name: VarTable{env, set, lb, ub, "
                  "HighsVarType::kContinuous, \"Make\"}.");
  }

  /// @hidden - exists only to reject `nullptr` as a name with a message
  template <typename Lb, typename Ub>
  VarTable(HighsEnv& /*env*/,
           const IndexSet<Keys...>& set,
           const Lb& /*lb*/,
           const Ub& /*ub*/,
           HighsVarType /*type*/,
           std::nullptr_t /*name*/)
      : Core{set} {
    static_assert(internal::always_false<Lb>, DREAMOR_INTERNAL_NULL_NAME_MESSAGE);
  }

  /// @anchor vartable-constructor
  /// Constructs a table with one new variable for each row of `set`, added to the model of `env` in the order of the
  /// set.
  ///
  /// If `name` is not empty, each variable is named `<name>_<key0>_<key1>...` from the values of its row. A name
  /// longer than 255 characters is cut to 255 characters, and a warning is logged. If `set` is empty, the table adds
  /// no columns.
  ///
  /// Takes: `lb` and `ub` must each be an arithmetic value, or a column callable that reads columns of `set` and
  /// returns a number, such as a `ParamTable` on `set`. If `lb` is left out, it is 0. If `ub` is left out, it is
  /// `kHighsInf`. `type` is HiGHS's variable type, as highspy's `addVariable` takes it, and is passed to HiGHS as it
  /// is, for example `kInteger`, `kSemiContinuous` or `kSemiInteger`. If `type` is left out, it is
  /// `HighsVarType::kContinuous`. If `name` is left out or empty, the variables have no names.
  /// Throws: `HighsCallError` if HiGHS rejects the columns or their names after the first call to `env.highs()`.
  /// Cost: Before the first call to `env.highs()`, the columns are written into the collected model. After that call,
  /// the constructor makes one `addVars` call, one `changeColsIntegrality` call if `type` is not continuous, and one
  /// `passColName` call for each variable that has a name.
  template <typename Lb = double, typename Ub = double>
  VarTable(HighsEnv& env,
           const IndexSet<Keys...>& set,
           const Lb& lb          = 0,
           const Ub& ub          = kHighsInf,
           HighsVarType type     = HighsVarType::kContinuous,
           std::string_view name = {})
      : Core{set} {
    internal::checkBound<Lb, key_type>();
    internal::checkBound<Ub, key_type>();
    const internal::Rows<key_type>& rows = this->rows();
    const std::size_t count              = rows.size();
    if (count == 0) { return; }
    const auto bounds = [&](std::span<double> lower, std::span<double> upper) {
      for (std::size_t i = 0; i < count; ++i) {
        lower[i] = internal::boundOf(lb, rows[i]);
        upper[i] = internal::boundOf(ub, rows[i]);
      }
    };
    const HighsInt first             = detail::HighsEnvAccess::addColumns(env, count, type, bounds);
    this->vars_                      = detail::HighsColumns{first, env, count};
    typename Core::NameBuilder names = Core::namesFor(name);
    if (!names.enabled()) { return; }
    for (std::size_t i = 0; i < count; ++i) {
      detail::HighsEnvAccess::nameColumn(env, first + static_cast<HighsInt>(i), Core::nameOf(names, rows[i]));
    }
  }

  /// Returns the value of each variable of the table in the primal solution that the `Highs` of `env` holds.
  ///
  /// A primal solution counts only if HiGHS reports it as feasible. After a solve that finds the model infeasible,
  /// HiGHS still holds the point at which it stopped. `solution` then throws and does not return that point.
  ///
  /// Returns: A `SolutionTable` on the set of the table, which looks up a value by the same keys as the table.
  /// Throws: `std::logic_error` if the `Highs` holds no feasible primal solution; `HighsCallError` if HiGHS rejects the
  /// collected model at the first call to `env.highs()`.
  /// Contract: highs_expression_of_its_environment
  [[nodiscard]] SolutionTable<Keys...> solution(const HighsEnv& env) const {
    if (!detail::hasPrimalSolution(env)) {
      throw std::logic_error(
          "VarTable::solution: the model has no feasible primal solution - run() it, and check "
          "its model status.");
    }
    const HighsSolution& solved = env.highs().getSolution();
    std::vector<double> values;
    values.reserve(this->vars_.size());
    if (this->vars_.size() > 0) { detail::checkHighsModel(&this->vars_[0].env(), env, "VarTable::solution"); }
    for (std::size_t i = 0; i < this->vars_.size(); ++i) {
      values.push_back(solved.col_value[static_cast<std::size_t>(this->vars_[i].index())]);
    }
    return this->solutionTable(std::move(values));
  }
};

/// Adds `row` to the model of `env` as a constraint.
///
/// HiGHS rejects a row that names a column twice. `addConstr` therefore adds up the terms over the same column
/// before it passes the row to HiGHS, and leaves out a column whose coefficients add up to zero. If `name` is empty,
/// the row has no name. HiGHS itself leaves out of its matrix a coefficient whose magnitude is at most its option
/// `small_matrix_value`, 1e-9 by default, such as what is left of terms that nearly cancel, `0.6 - 0.4 - 0.2`.
///
/// Takes: `row` must be a row: a `HighsBoundedLinearExpression`, a `HighsLowerBoundedLinearExpression` or a
/// `HighsUpperBoundedLinearExpression`.
/// Returns: The new constraint.
/// Throws: `HighsCallError` if HiGHS rejects the row or its name after the first call to `env.highs()`;
/// `std::length_error` if the rows collected before the first call to `env.highs()` hold more terms in total than
/// `HighsInt` can count.
/// Contract: highs_expression_of_its_environment, highs_finite_numbers
template <typename Row>
HighsCons addConstr(HighsEnv& env, Row&& row, std::string_view name = {});

/// @hidden - exists only to reject `nullptr` as a name with a message
template <typename Row>
HighsCons addConstr(HighsEnv& env, Row&& row, std::nullptr_t /*name*/) {
  static_assert(internal::always_false<Row>, DREAMOR_INTERNAL_NULL_NAME_MESSAGE);
  return addConstr(env, std::forward<Row>(row));
}

/// @hidden - the definition of addConstr, declared above
template <typename Row>
HighsCons addConstr(HighsEnv& env, Row&& row, std::string_view name) {
  using Kind = std::remove_cvref_t<Row>;
  if constexpr (is_highs_row<Kind>) {
    // A two-sided row is read where it is. A one-sided row is converted to a two-sided row first.
    const auto add = [&env, name](const HighsBoundedLinearExpression& bounded) {
      const HighsLinearExpression& expression = bounded.expression();
      detail::checkHighsModel(detail::HighsModelAccess::of(expression), env, "addConstr");
      detail::checkHighsRow(expression, bounded.lower(), bounded.upper());
      const HighsCons added{detail::HighsEnvAccess::addRow(
                                env, bounded.lower(), bounded.upper(), expression.indices(), expression.coefficients()),
                            env};
      if (!name.empty()) { detail::HighsEnvAccess::nameRow(env, added.index(), name); }
      return added;
    };
    if constexpr (std::same_as<Kind, HighsBoundedLinearExpression>) {
      return add(row);
    } else {
      return add(HighsBoundedLinearExpression{std::forward<Row>(row)});
    }
  } else {
    static_assert(internal::always_false<Kind>,
                  "addConstr takes a row - an expression with its bounds: addConstr(env, x + y <= 1), or "
                  "addConstr(env, (0 <= x - y) <= 5).");
    return HighsCons{0, env};
  }
}

/// Replaces the objective of the model of `env` with `objective`.
///
/// The cost of every column, the constant of the objective and the Hessian are replaced. A column that `objective`
/// does not contain gets the cost 0. A linear objective removes the Hessian of an earlier quadratic objective. The
/// sense of the objective does not change. If no sense was set, HiGHS minimizes.
///
/// HiGHS minimizes or maximizes `c'x + 1/2 x'Qx`. `setObjective` puts twice the coefficient of each square into the
/// diagonal of `Q`, so the objective that HiGHS solves equals `objective` as written.
///
/// Takes: `objective` must be a number, a `HighsVar`, a `HighsLinearTerm`, a `HighsLinearExpression`, a
/// `HighsQuadraticTerm` or a `HighsQuadraticExpression`. In particular, `objective` must not be a row, and a sum must
/// be made into an expression with `build()` first.
/// Throws: `HighsCallError` if HiGHS rejects the objective after the first call to `env.highs()`.
/// Contract: highs_expression_of_its_environment, highs_finite_numbers
template <typename Objective>
void setObjective(HighsEnv& env, const Objective& objective) {
  using Kind = std::remove_cvref_t<Objective>;
  if constexpr (internal::kIsUnbuiltSum<Kind>) {
    static_assert(internal::always_false<Kind>, DREAMOR_INTERNAL_SUM_NOT_BUILT_MESSAGE);
  } else if constexpr (is_highs_row<Kind>) {
    static_assert(internal::always_false<Kind>,
                  "An objective is an expression, not a row: setObjective(env, x + y), without the bound.");
  } else if constexpr (std::same_as<Kind, HighsQuadraticExpression>) {
    detail::HighsEnvAccess::setObjective(env, objective.linear(), &objective);
  } else if constexpr (std::same_as<Kind, HighsQuadraticTerm>) {
    const HighsQuadraticExpression quadratic{objective};
    detail::HighsEnvAccess::setObjective(env, quadratic.linear(), &quadratic);
  } else if constexpr (is_highs_linear<Kind>) {
    detail::HighsEnvAccess::setObjective(env, detail::toHighsLinear(objective), nullptr);
  } else {
    static_assert(internal::always_false<Kind>,
                  "setObjective takes an expression of this backend's variables: a HighsVar, a term, a "
                  "HighsLinearExpression or a HighsQuadraticExpression.");
  }
}

/// @overload
/// Replaces the objective of the model of `env` with `objective`, and sets the sense of the objective to `sense`.
///
/// The cost of every column, the constant of the objective and the Hessian are replaced. A column that `objective`
/// does not contain gets the cost 0. A linear objective removes the Hessian of an earlier quadratic objective.
///
/// HiGHS minimizes or maximizes `c'x + 1/2 x'Qx`. `setObjective` puts twice the coefficient of each square into the
/// diagonal of `Q`, so the objective that HiGHS solves equals `objective` as written.
///
/// Takes: `objective` must be a number, a `HighsVar`, a `HighsLinearTerm`, a `HighsLinearExpression`, a
/// `HighsQuadraticTerm` or a `HighsQuadraticExpression`. In particular, `objective` must not be a row, and a sum must
/// be made into an expression with `build()` first. `sense` must be `ObjSense::kMinimize` or `ObjSense::kMaximize`.
/// Throws: `HighsCallError` if HiGHS rejects the objective or the sense after the first call to `env.highs()`.
/// Contract: highs_expression_of_its_environment, highs_finite_numbers
template <typename Objective>
void setObjective(HighsEnv& env, const Objective& objective, ObjSense sense) {
  setObjective(env, objective);
  detail::HighsEnvAccess::setSense(env, sense);
}

/// Sets `objective` as the objective to minimize, and then runs the `Highs` of `env`.
///
/// `minimize` calls `setObjective` with `ObjSense::kMinimize`, and then calls `run()` on `env.highs()`. A status of
/// `HighsStatus::kError` from `run()` is returned, not thrown. `minimize` corresponds to highspy's `minimize`.
///
/// Takes: `objective` must be an objective that `setObjective` accepts.
/// Returns: The status that `run()` returns.
/// Throws: `HighsCallError` if HiGHS rejects the collected model or the objective.
/// Contract: highs_expression_of_its_environment, highs_finite_numbers
template <typename Objective>
HighsStatus minimize(HighsEnv& env, const Objective& objective) {
  setObjective(env, objective, ObjSense::kMinimize);
  return env.highs().run();
}

/// Sets `objective` as the objective to maximize, and then runs the `Highs` of `env`.
///
/// `maximize` calls `setObjective` with `ObjSense::kMaximize`, and then calls `run()` on `env.highs()`. A status of
/// `HighsStatus::kError` from `run()` is returned, not thrown. `maximize` corresponds to highspy's `maximize`.
///
/// Takes: `objective` must be an objective that `setObjective` accepts.
/// Returns: The status that `run()` returns.
/// Throws: `HighsCallError` if HiGHS rejects the collected model or the objective.
/// Contract: highs_expression_of_its_environment, highs_finite_numbers
template <typename Objective>
HighsStatus maximize(HighsEnv& env, const Objective& objective) {
  setObjective(env, objective, ObjSense::kMaximize);
  return env.highs().run();
}

/// Returns the value of `expression` in the primal solution that the `Highs` of `env` holds.
///
/// The value is the constant of `expression` plus, for each term, the coefficient times the value of the variable.
/// A primal solution counts only if HiGHS reports it as feasible. For the values of all variables of a `VarTable`,
/// use `VarTable::solution`. `val` corresponds to highspy's `val`.
///
/// Throws: `std::logic_error` if the `Highs` holds no feasible primal solution; `HighsCallError` if HiGHS rejects the
/// collected model at the first call to `env.highs()`.
/// Contract: highs_expression_of_its_environment
inline double val(const HighsEnv& env, const HighsLinearExpression& expression) {
  if (!detail::hasPrimalSolution(env)) {
    throw std::logic_error("val: the model has no feasible primal solution - run() it, and check its model status.");
  }
  detail::checkHighsModel(detail::HighsModelAccess::of(expression), env, "val");
  return detail::linearValue(env.highs().getSolution().col_value, expression);
}

/// @overload
/// Returns the value of `expression` in the primal solution that the `Highs` of `env` holds.
///
/// The value is the constant of `expression`, plus, for each linear term, the coefficient times the value of the
/// variable, plus, for each quadratic term, the coefficient times the values of its two variables. A QP's objective
/// value is also HiGHS's own, `env.highs().getInfo().objective_function_value`; `val` gives the value of any quadratic
/// expression, such as one part of that objective. A primal solution counts only if HiGHS reports it as feasible.
///
/// Throws: `std::logic_error` if the `Highs` holds no feasible primal solution; `HighsCallError` if HiGHS rejects the
/// collected model at the first call to `env.highs()`.
/// Contract: highs_expression_of_its_environment
inline double val(const HighsEnv& env, const HighsQuadraticExpression& expression) {
  if (!detail::hasPrimalSolution(env)) {
    throw std::logic_error("val: the model has no feasible primal solution - run() it, and check its model status.");
  }
  detail::checkHighsModel(detail::HighsModelAccess::of(expression), env, "val");
  const std::vector<double>& x               = env.highs().getSolution().col_value;
  double value                               = detail::linearValue(x, expression.linear());
  const std::span<const HighsInt> first      = expression.first();
  const std::span<const HighsInt> second     = expression.second();
  const std::span<const double> coefficients = expression.quadraticCoefficients();
  for (std::size_t k = 0; k < first.size(); ++k) {
    value += coefficients[k] * x[static_cast<std::size_t>(first[k])] * x[static_cast<std::size_t>(second[k])];
  }
  return value;
}

/// @overload
/// Returns the value of `variable` in the primal solution that the `Highs` of `env` holds.
///
/// A primal solution counts only if HiGHS reports it as feasible. For the values of all variables of a `VarTable`,
/// use `VarTable::solution`.
///
/// Throws: `std::logic_error` if the `Highs` holds no feasible primal solution; `HighsCallError` if HiGHS rejects the
/// collected model at the first call to `env.highs()`.
/// Contract: highs_expression_of_its_environment
inline double val(const HighsEnv& env, const HighsVar& variable) {
  return val(env, HighsLinearExpression{variable});
}

/// Returns the sum of `term` over `rows`, before `build()`.
///
/// The Sums section describes the sum that `sumExpr` returns, with its `where`, `whereEach` and `build()`. On this
/// backend, `build()` returns a `HighsLinearExpression`. If `term` returns a `HighsQuadraticTerm` or a
/// `HighsQuadraticExpression`, `build()` returns a `HighsQuadraticExpression`.
///
/// `sumExpr` takes no `HighsEnv`, because an empty `HighsLinearExpression` belongs to no model.
///
/// Takes: `rows` must be a set, the rows of a group, or a container. If `rows` is a container that is not a set,
/// `Columns` must name the column that the elements of the container fill. Otherwise, `Columns` must be empty.
/// `term` must be a column callable that reads columns of `rows`, or a `VarTable` whose key columns are columns of
/// `rows`.
template <typename... Columns, typename Rows, typename Term>
[[nodiscard]] auto sumExpr(Rows&& rows, Term&& term) {
  return internal::makeSum<Columns...>(
      [] { return HighsLinearExpression{}; }, std::forward<Rows>(rows), std::forward<Term>(term));
}

/// @hidden - exists only to reject a `HighsEnv` as the first argument with a message
template <typename... Columns, typename First, typename Rows, typename Term>
auto sumExpr(First&& /*first*/, Rows&& /*rows*/, Term&& /*term*/) {
  static_assert(
      internal::always_false<First>,
      "HiGHS's sumExpr takes no HighsEnv: sumExpr(rows, term). An empty HighsLinearExpression needs no model.");
}

}  // namespace dreamor::highs

// A quadratic term makes the sum a HighsQuadraticExpression. A linear term keeps the sum linear.
template <typename Term>
  requires(dreamor::highs::is_highs_quadratic<Term>)
struct dreamor::internal::SumTotal<dreamor::highs::HighsLinearExpression, Term> {
  using type = dreamor::highs::HighsQuadraticExpression;
};
