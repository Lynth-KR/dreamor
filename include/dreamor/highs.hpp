// SPDX-License-Identifier: Apache-2.0
// File: highs.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// The HiGHS backend: dreamor::highs::VarTable and dreamor::highs::sumOf, on HiGHS's own C++ API.
//
// HiGHS's C++ API is a matrix API: a column is an index, a row is (lower, upper, indices, values),
// and a quadratic objective is a Hessian. It has no expression to write a model in. highspy, the
// HiGHS team's Python interface, adds one on top - highs_var, highs_linear_expression, highs_cons,
// addConstr, setObjective, minimize - and this backend brings that declarative interface to C++, its
// names in dreamor's case:
//
//     namespace hs = dreamor::highs;
//     hs::HighsEnv env;
//     const hs::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty,
//                                                               HighsVarType::kContinuous, "Yield"};
//
//     const hs::HighsLinearExpression totalLoad = hs::sumOf(rows, machine_usage).build();
//     hs::addConstr(env, totalLoad <= machine->capacity);
//
//     hs::minimize(env, hs::sumOf(sPRODUCE, unit_cost).build());   // sets the objective, then run()
//     const auto yieldPlan = vYield.solution(env);
//
//   highspy                        dreamor::highs
//   Highs                          HighsEnv            the model as written, and the Highs that solves it
//   highs_var                      HighsVar            a column and its environment
//   highs_linear_expression        HighsLinearExpression
//   (none)                         HighsBoundedLinearExpression   an expression with its bounds: a row
//   (none)                         HighsQuadraticExpression       objective only, as HiGHS takes it
//   highs_cons                     HighsCons           a row and its environment
//   Highs.addVariable              VarTable            a family of variables at once
//   Highs.addConstr(expr, name)    addConstr(env, row, name)
//   Highs.setObjective(expr, s)    setObjective(env, objective, sense)
//   Highs.minimize / maximize      minimize(env, objective) / maximize    then run(), as highspy does
//   Highs.qsum                     sumOf
//
// Where it differs from highspy, and why:
//   - a row is a type of its own: x + y <= 1 is a HighsBoundedLinearExpression, so an inequality
//     handed to setObjective stops at compile time (highspy raises at run time);
//   - a quadratic expression is compared to nothing: HiGHS takes quadratic terms only in the
//     objective, so `x * x <= 1` stops at compile time;
//   - the model is collected and reaches the Highs in one passModel, the first time the Highs is
//     reached (HighsEnv); highspy hands HiGHS every variable and row as it is written, and HiGHS pays
//     for each change;
//   - setObjective replaces the Hessian too, so a quadratic objective does not outlive a linear
//     one set after it;
//   - an operation does not copy the expression: a term is a value (HighsLinearTerm), and a sum
//     appends to its operands' buffers.
//
// HiGHS refuses a row or a Hessian that names a column twice, and keeps a zero it is given. An
// expression appends its terms as they come - x + x is two terms - and addConstr and setObjective
// merge them once, as they pass them on, in time linear in the terms (a per-thread workspace, no
// sort), dropping the ones that cancel.
//
// A HighsVar knows its environment. Mixing two environments' variables in one expression, or handing
// an environment a row of another's, is a broken contract: a debug build stops on it, a release build
// does not check.
//
// The rest is the library's and the same as on every backend: bounds as numbers or column
// callables, names "<name>_<key0>_<key1>...", vYield(row) throwing std::out_of_range for a row
// outside the set, iteration in the set's order, and sumOf's where() / split(). As on MathOpt,
// solution() throws std::logic_error when the model has no primal solution, and sumOf takes no
// first argument. A HiGHS call that fails throws HighsError, which carries its HighsStatus.
//
// Building: add HiGHS's include/highs and link libhighs (tests/run_tests.sh --highs). The
// HiGHS that OR-Tools ships (include/highs, lib/libhighs.so in its prebuilt) is one too.
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
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"
#include "dreamor/internal/row_index.hpp"
#include "dreamor/internal/sum_builder.hpp"
#include "dreamor/internal/var_table_core.hpp"
#include "dreamor/logger/logger_light.hpp"
#include "dreamor/value_table.hpp"

#include "dreamor/general/third_party_headers.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#include "Highs.h"
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::internal {
struct HighsModelAccess;
struct HighsEnvAccess;
}  // namespace dreamor::internal

namespace dreamor::highs {

// A HiGHS call that failed (returned HighsStatus::kError): which call, and its status. highspy
// raises a plain Exception there; this one keeps the status.
class HighsError : public std::runtime_error {
 public:
  HighsError(const char* call, HighsStatus status)
      : std::runtime_error{std::string{"dreamor::highs: Highs::"} + call + " returned " + highsStatusToString(status)},
        call_{call},
        status_{status} {}

  [[nodiscard]] const char* call() const noexcept { return call_; }
  [[nodiscard]] HighsStatus status() const noexcept { return status_; }

 private:
  const char* call_;  // a string literal: the name of the Highs method
  HighsStatus status_;
};

class HighsEnv;

// A column of a model, as highspy's highs_var: its index and the environment it belongs to.
class HighsVar {
 public:
  HighsVar(HighsInt index, const HighsEnv& env) noexcept : index_{index}, env_{&env} {}

  [[nodiscard]] HighsInt index() const noexcept { return index_; }
  [[nodiscard]] const HighsEnv& env() const noexcept { return *env_; }

 private:
  HighsInt index_;
  const HighsEnv* env_;
};

// A row of a model, as highspy's highs_cons: addConstr returns one.
class HighsCons {
 public:
  HighsCons(HighsInt index, const HighsEnv& env) noexcept : index_{index}, env_{&env} {}

  [[nodiscard]] HighsInt index() const noexcept { return index_; }
  [[nodiscard]] const HighsEnv& env() const noexcept { return *env_; }

 private:
  HighsInt index_;
  const HighsEnv* env_;
};

// coefficient * variable: what 3 * x makes, with no expression to allocate.
struct HighsLinearTerm {
  HighsVar variable;
  double coefficient;
};

// coefficient * first * second: what x * y or 2 * x * x makes.
struct HighsQuadraticTerm {
  HighsVar first;
  HighsVar second;
  double coefficient;
};

class HighsQuadraticExpression;

// sum of coefficient * variable, plus a constant - highspy's highs_linear_expression, without
// its bounds (a row is HighsBoundedLinearExpression). Terms are appended as they come and
// merged when the expression is handed to the model.
class HighsLinearExpression {
 public:
  HighsLinearExpression() = default;
  // NOLINTBEGIN(*-explicit-constructor): a number, a variable and a term are expressions, as in highspy
  HighsLinearExpression(double constant) noexcept : constant_{constant} {}
  HighsLinearExpression(HighsVar variable) { append(variable, 1.0); }
  HighsLinearExpression(const HighsLinearTerm& term) { append(term.variable, term.coefficient); }
  // NOLINTEND(*-explicit-constructor)

  HighsLinearExpression& operator+=(HighsVar variable) {
    append(variable, 1.0);
    return *this;
  }
  HighsLinearExpression& operator+=(const HighsLinearTerm& term) {
    append(term.variable, term.coefficient);
    return *this;
  }
  HighsLinearExpression& operator+=(double constant) noexcept {
    constant_ += constant;
    return *this;
  }
  HighsLinearExpression& operator+=(const HighsLinearExpression& other) {
    appendAll(other, 1.0);
    constant_ += other.constant_;
    return *this;
  }
  HighsLinearExpression& operator+=(HighsLinearExpression&& other) {
    if (indices_.empty()) {  // take the other's buffers instead of copying into empty ones
      const double constant = constant_;
      *this                 = std::move(other);
      constant_ += constant;
      return *this;
    }
    return *this += static_cast<const HighsLinearExpression&>(other);
  }

  HighsLinearExpression& operator-=(HighsVar variable) {
    append(variable, -1.0);
    return *this;
  }
  HighsLinearExpression& operator-=(const HighsLinearTerm& term) {
    append(term.variable, -term.coefficient);
    return *this;
  }
  HighsLinearExpression& operator-=(double constant) noexcept {
    constant_ -= constant;
    return *this;
  }
  HighsLinearExpression& operator-=(const HighsLinearExpression& other) {
    appendAll(other, -1.0);
    constant_ -= other.constant_;
    return *this;
  }

  HighsLinearExpression& operator*=(double factor) noexcept {
    for (double& coefficient : coefficients_) {
      coefficient *= factor;
    }
    constant_ *= factor;
    return *this;
  }
  HighsLinearExpression& operator/=(double divisor) noexcept { return *this *= 1.0 / divisor; }

  // The terms as appended - a column may appear more than once - and the constant.
  [[nodiscard]] std::span<const HighsInt> indices() const noexcept { return indices_; }
  [[nodiscard]] std::span<const double> coefficients() const noexcept { return coefficients_; }
  [[nodiscard]] double constant() const noexcept { return constant_; }
  [[nodiscard]] std::size_t size() const noexcept { return indices_.size(); }

 private:
  friend class HighsQuadraticExpression;
  friend struct internal::HighsModelAccess;

  // Appends sign * every term of other. Room is made first and the terms are read by index, so
  // other may be this expression itself (e += e): nothing reallocates under the reads.
  void appendAll(const HighsLinearExpression& other, double sign) {
    adopt(other.env_);
    const std::size_t count = other.indices_.size();
    indices_.reserve(indices_.size() + count);
    coefficients_.reserve(coefficients_.size() + count);
    for (std::size_t k = 0; k < count; ++k) {
      indices_.push_back(other.indices_[k]);
      coefficients_.push_back(sign * other.coefficients_[k]);
    }
  }

  void append(HighsVar variable, double coefficient) {
    adopt(&variable.env());
    indices_.push_back(variable.index());
    coefficients_.push_back(coefficient);
  }

  // Every variable of an expression belongs to one environment: a debug build stops on a second.
  void adopt(const HighsEnv* env) noexcept {
    if (env == nullptr) { return; }
    if (env_ == nullptr) {
      env_ = env;
      return;
    }
#ifndef NDEBUG
    if (env != env_) {
      DREAMOR_DEBUG_FATAL(
          "dreamor::highs: one expression holds variables of two HighsEnvs - a broken contract (dreamor/highs.hpp).");
    }
#endif
  }

  std::vector<HighsInt> indices_;
  std::vector<double> coefficients_;
  double constant_{0.0};
  const HighsEnv* env_{nullptr};
};

// A linear expression plus quadratic terms: an objective, the only place HiGHS takes them.
class HighsQuadraticExpression {
 public:
  HighsQuadraticExpression() = default;
  // NOLINTBEGIN(*-explicit-constructor): a linear expression and a quadratic term are quadratic expressions
  HighsQuadraticExpression(HighsLinearExpression linear) : linear_{std::move(linear)} {}
  HighsQuadraticExpression(const HighsQuadraticTerm& term) { append(term); }
  // NOLINTEND(*-explicit-constructor)

  HighsQuadraticExpression& operator+=(const HighsQuadraticTerm& term) {
    append(term);
    return *this;
  }
  HighsQuadraticExpression& operator+=(const HighsQuadraticExpression& other) {
    appendAll(other, 1.0);
    return *this;
  }
  template <typename Linear>
    requires std::constructible_from<HighsLinearExpression, Linear&&>
  HighsQuadraticExpression& operator+=(Linear&& linear) {
    linear_ += std::forward<Linear>(linear);
    return *this;
  }

  HighsQuadraticExpression& operator-=(const HighsQuadraticTerm& term) {
    append(HighsQuadraticTerm{.first = term.first, .second = term.second, .coefficient = -term.coefficient});
    return *this;
  }
  HighsQuadraticExpression& operator-=(const HighsQuadraticExpression& other) {
    appendAll(other, -1.0);
    return *this;
  }
  template <typename Linear>
    requires std::constructible_from<HighsLinearExpression, Linear&&>
  HighsQuadraticExpression& operator-=(Linear&& linear) {
    linear_ -= HighsLinearExpression{std::forward<Linear>(linear)};
    return *this;
  }

  HighsQuadraticExpression& operator*=(double factor) noexcept {
    linear_ *= factor;
    for (double& coefficient : quadratic_coefficients_) {
      coefficient *= factor;
    }
    return *this;
  }
  HighsQuadraticExpression& operator/=(double divisor) noexcept { return *this *= 1.0 / divisor; }

  [[nodiscard]] const HighsLinearExpression& linear() const noexcept { return linear_; }
  // The quadratic terms as appended, each with first() >= second() (the lower triangle).
  [[nodiscard]] std::span<const HighsInt> first() const noexcept { return first_; }
  [[nodiscard]] std::span<const HighsInt> second() const noexcept { return second_; }
  [[nodiscard]] std::span<const double> quadraticCoefficients() const noexcept { return quadratic_coefficients_; }

 private:
  friend struct internal::HighsModelAccess;

  // As HighsLinearExpression::appendAll: other may be this expression itself.
  void appendAll(const HighsQuadraticExpression& other, double sign) {
    if (sign > 0) {
      linear_ += other.linear_;
    } else {
      linear_ -= other.linear_;
    }
    const std::size_t count = other.first_.size();
    first_.reserve(first_.size() + count);
    second_.reserve(second_.size() + count);
    quadratic_coefficients_.reserve(quadratic_coefficients_.size() + count);
    for (std::size_t k = 0; k < count; ++k) {
      first_.push_back(other.first_[k]);
      second_.push_back(other.second_[k]);
      quadratic_coefficients_.push_back(sign * other.quadratic_coefficients_[k]);
    }
  }

  void append(const HighsQuadraticTerm& term) {
    linear_.adopt(&term.first.env());
    linear_.adopt(&term.second.env());
    first_.push_back(std::max(term.first.index(), term.second.index()));
    second_.push_back(std::min(term.first.index(), term.second.index()));
    quadratic_coefficients_.push_back(term.coefficient);
  }

  HighsLinearExpression linear_;
  std::vector<HighsInt> first_;
  std::vector<HighsInt> second_;
  std::vector<double> quadratic_coefficients_;
};

// lower <= expression <= upper: one row. The expression's constant is moved into the bounds.
class HighsBoundedLinearExpression {
 public:
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): lower, then upper, as every HiGHS row takes them
  HighsBoundedLinearExpression(HighsLinearExpression expression, double lower, double upper)
      : expression_{std::move(expression)}, lower_{lower}, upper_{upper} {
    const double constant = expression_.constant();
    expression_ -= constant;
    lower_ -= constant;
    upper_ -= constant;
  }

  [[nodiscard]] const HighsLinearExpression& expression() const noexcept { return expression_; }
  [[nodiscard]] double lower() const noexcept { return lower_; }
  [[nodiscard]] double upper() const noexcept { return upper_; }

 private:
  HighsLinearExpression expression_;
  double lower_;
  double upper_;
};

// lower <= expression, and expression <= upper: rows that can take the other bound, as in
// (lo <= x + y) <= hi.
class HighsLowerBoundedLinearExpression {
 public:
  HighsLowerBoundedLinearExpression(HighsLinearExpression expression, double lower)
      : expression_{std::move(expression)}, lower_{lower} {}
  // NOLINTNEXTLINE(*-explicit-constructor): it is a row as it stands
  operator HighsBoundedLinearExpression() const& { return {expression_, lower_, kHighsInf}; }
  // NOLINTNEXTLINE(*-explicit-constructor): it is a row as it stands
  operator HighsBoundedLinearExpression() && { return {std::move(expression_), lower_, kHighsInf}; }

  friend HighsBoundedLinearExpression operator<=(HighsLowerBoundedLinearExpression row, double upper) {
    return {std::move(row.expression_), row.lower_, upper};
  }

 private:
  HighsLinearExpression expression_;
  double lower_;
};

class HighsUpperBoundedLinearExpression {
 public:
  HighsUpperBoundedLinearExpression(HighsLinearExpression expression, double upper)
      : expression_{std::move(expression)}, upper_{upper} {}
  // NOLINTNEXTLINE(*-explicit-constructor): it is a row as it stands
  operator HighsBoundedLinearExpression() const& { return {expression_, -kHighsInf, upper_}; }
  // NOLINTNEXTLINE(*-explicit-constructor): it is a row as it stands
  operator HighsBoundedLinearExpression() && { return {std::move(expression_), -kHighsInf, upper_}; }

  friend HighsBoundedLinearExpression operator>=(HighsUpperBoundedLinearExpression row, double lower) {
    return {std::move(row.expression_), lower, row.upper_};
  }

 private:
  HighsLinearExpression expression_;
  double upper_;
};

// What the operators take. A number is any arithmetic type but bool.
template <typename T>
inline constexpr bool is_highs_scalar = std::is_arithmetic_v<T> && !std::same_as<T, bool>;
template <typename T>
inline constexpr bool is_highs_linear_term =
    std::same_as<T, HighsVar> || std::same_as<T, HighsLinearTerm> || std::same_as<T, HighsLinearExpression>;
template <typename T>
inline constexpr bool is_highs_linear = is_highs_scalar<T> || is_highs_linear_term<T>;
template <typename T>
inline constexpr bool is_highs_quadratic =
    std::same_as<T, HighsQuadraticTerm> || std::same_as<T, HighsQuadraticExpression>;
template <typename T>
inline constexpr bool is_highs_row =
    std::same_as<T, HighsBoundedLinearExpression> || std::same_as<T, HighsLowerBoundedLinearExpression> ||
    std::same_as<T, HighsUpperBoundedLinearExpression>;

// Two operands of a linear operation: at least one of them a variable, a term or an expression.
template <typename L, typename R>
concept LinearOperands = is_highs_linear<std::remove_cvref_t<L>> && is_highs_linear<std::remove_cvref_t<R>> &&
                         (is_highs_linear_term<std::remove_cvref_t<L>> || is_highs_linear_term<std::remove_cvref_t<R>>);
// Two operands of a quadratic sum: at least one quadratic, the other quadratic or linear.
template <typename L, typename R>
concept QuadraticOperands =
    (is_highs_quadratic<std::remove_cvref_t<L>> || is_highs_quadratic<std::remove_cvref_t<R>>) &&
    (is_highs_quadratic<std::remove_cvref_t<L>> || is_highs_linear<std::remove_cvref_t<L>>) &&
    (is_highs_quadratic<std::remove_cvref_t<R>> || is_highs_linear<std::remove_cvref_t<R>>);
template <typename T>
concept Scalar = is_highs_scalar<std::remove_cvref_t<T>>;

}  // namespace dreamor::highs

namespace dreamor::internal {

// The model an expression's variables belong to, null while it has none: for the debug checks
// and the expansion of a product, not for users - no raw pointer leaves the public API.
struct HighsModelAccess {
  [[nodiscard]] static const dreamor::highs::HighsEnv* of(
      const dreamor::highs::HighsLinearExpression& expression) noexcept {
    return expression.env_;
  }
  [[nodiscard]] static const dreamor::highs::HighsEnv* of(
      const dreamor::highs::HighsQuadraticExpression& expression) noexcept {
    return expression.linear_.env_;
  }
};

// An operand as an expression: a number by value (no braces, which would narrow an int), an
// expression moved when it is an rvalue.
template <typename T>
[[nodiscard]] dreamor::highs::HighsLinearExpression toHighsLinear(T&& operand) {
  if constexpr (dreamor::highs::is_highs_scalar<std::remove_cvref_t<T>>) {
    return dreamor::highs::HighsLinearExpression(static_cast<double>(operand));
  } else {
    return dreamor::highs::HighsLinearExpression(std::forward<T>(operand));
  }
}
template <typename T>
[[nodiscard]] dreamor::highs::HighsQuadraticExpression toHighsQuadratic(T&& operand) {
  if constexpr (dreamor::highs::is_highs_quadratic<std::remove_cvref_t<T>>) {
    return dreamor::highs::HighsQuadraticExpression(std::forward<T>(operand));
  } else {
    return dreamor::highs::HighsQuadraticExpression(toHighsLinear(std::forward<T>(operand)));
  }
}

}  // namespace dreamor::internal

namespace dreamor::highs {

// ---------------------------------------------------------------------------------------
// Negation and scaling

inline HighsLinearTerm operator-(HighsVar variable) noexcept {
  return {.variable = variable, .coefficient = -1.0};
}
inline HighsLinearTerm operator-(const HighsLinearTerm& term) noexcept {
  return {.variable = term.variable, .coefficient = -term.coefficient};
}
inline HighsLinearExpression operator-(HighsLinearExpression expression) {
  expression *= -1.0;
  return expression;
}
inline HighsQuadraticTerm operator-(const HighsQuadraticTerm& term) noexcept {
  return {.first = term.first, .second = term.second, .coefficient = -term.coefficient};
}
inline HighsQuadraticExpression operator-(HighsQuadraticExpression expression) {
  expression *= -1.0;
  return expression;
}

template <Scalar S>
HighsLinearTerm operator*(S scale, HighsVar variable) noexcept {
  return {.variable = variable, .coefficient = static_cast<double>(scale)};
}
template <Scalar S>
HighsLinearTerm operator*(HighsVar variable, S scale) noexcept {
  return {.variable = variable, .coefficient = static_cast<double>(scale)};
}
template <Scalar S>
HighsLinearTerm operator*(S scale, const HighsLinearTerm& term) noexcept {
  return {.variable = term.variable, .coefficient = static_cast<double>(scale) * term.coefficient};
}
template <Scalar S>
HighsLinearTerm operator*(const HighsLinearTerm& term, S scale) noexcept {
  return {.variable = term.variable, .coefficient = static_cast<double>(scale) * term.coefficient};
}
template <Scalar S>
HighsLinearExpression operator*(S scale, HighsLinearExpression expression) {
  expression *= static_cast<double>(scale);
  return expression;
}
template <Scalar S>
HighsLinearExpression operator*(HighsLinearExpression expression, S scale) {
  expression *= static_cast<double>(scale);
  return expression;
}
template <Scalar S>
HighsQuadraticTerm operator*(S scale, const HighsQuadraticTerm& term) noexcept {
  return {.first = term.first, .second = term.second, .coefficient = static_cast<double>(scale) * term.coefficient};
}
template <Scalar S>
HighsQuadraticTerm operator*(const HighsQuadraticTerm& term, S scale) noexcept {
  return {.first = term.first, .second = term.second, .coefficient = static_cast<double>(scale) * term.coefficient};
}
template <Scalar S>
HighsQuadraticExpression operator*(S scale, HighsQuadraticExpression expression) {
  expression *= static_cast<double>(scale);
  return expression;
}
template <Scalar S>
HighsQuadraticExpression operator*(HighsQuadraticExpression expression, S scale) {
  expression *= static_cast<double>(scale);
  return expression;
}

template <Scalar S>
HighsLinearTerm operator/(HighsVar variable, S divisor) noexcept {
  return {.variable = variable, .coefficient = 1.0 / static_cast<double>(divisor)};
}
template <Scalar S>
HighsLinearTerm operator/(const HighsLinearTerm& term, S divisor) noexcept {
  return {.variable = term.variable, .coefficient = term.coefficient / static_cast<double>(divisor)};
}
template <Scalar S>
HighsLinearExpression operator/(HighsLinearExpression expression, S divisor) {
  expression /= static_cast<double>(divisor);
  return expression;
}
template <Scalar S>
HighsQuadraticTerm operator/(const HighsQuadraticTerm& term, S divisor) noexcept {
  return {.first = term.first, .second = term.second, .coefficient = term.coefficient / static_cast<double>(divisor)};
}
template <Scalar S>
HighsQuadraticExpression operator/(HighsQuadraticExpression expression, S divisor) {
  expression /= static_cast<double>(divisor);
  return expression;
}

// ---------------------------------------------------------------------------------------
// Products of two linear operands

inline HighsQuadraticTerm operator*(HighsVar first, HighsVar second) noexcept {
  return {.first = first, .second = second, .coefficient = 1.0};
}
inline HighsQuadraticTerm operator*(const HighsLinearTerm& first, HighsVar second) noexcept {
  return {.first = first.variable, .second = second, .coefficient = first.coefficient};
}
inline HighsQuadraticTerm operator*(HighsVar first, const HighsLinearTerm& second) noexcept {
  return {.first = first, .second = second.variable, .coefficient = second.coefficient};
}
inline HighsQuadraticTerm operator*(const HighsLinearTerm& first, const HighsLinearTerm& second) noexcept {
  return {.first = first.variable, .second = second.variable, .coefficient = first.coefficient * second.coefficient};
}

// (a + b x)(c + d y), expanded: an expression times anything linear but a number.
template <typename L, typename R>
  requires(is_highs_linear_term<std::remove_cvref_t<L>> && is_highs_linear_term<std::remove_cvref_t<R>> &&
           (std::same_as<std::remove_cvref_t<L>, HighsLinearExpression> ||
            std::same_as<std::remove_cvref_t<R>, HighsLinearExpression>))
HighsQuadraticExpression operator*(const L& lhs, const R& rhs) {
  const HighsLinearExpression left  = internal::toHighsLinear(lhs);
  const HighsLinearExpression right = internal::toHighsLinear(rhs);
  const HighsEnv* left_model        = internal::HighsModelAccess::of(left);
  const HighsEnv* model             = left_model != nullptr ? left_model : internal::HighsModelAccess::of(right);
  // (a + l)(c + r) = ac + c l + a r + l r; a side whose constant is zero adds no terms.
  const double a = left.constant();
  const double c = right.constant();
  HighsQuadraticExpression product(a * c);
  // An expression has a model once it has a term: with none on either side, the product is ac.
  if (model == nullptr) { return product; }
  const auto scaled = [&product, model](const HighsLinearExpression& side, double factor) {
    if (std::fpclassify(factor) == FP_ZERO) { return; }
    for (std::size_t k = 0; k < side.size(); ++k) {
      product += HighsLinearTerm{
          .variable    = HighsVar{side.indices()[k], *model},
          .coefficient = factor * side.coefficients()[k],
      };
    }
  };
  scaled(left, c);
  scaled(right, a);
  for (std::size_t p = 0; p < left.size(); ++p) {
    for (std::size_t q = 0; q < right.size(); ++q) {
      product += HighsQuadraticTerm{
          .first       = HighsVar{left.indices()[p], *model},
          .second      = HighsVar{right.indices()[q], *model},
          .coefficient = left.coefficients()[p] * right.coefficients()[q],
      };
    }
  }
  return product;
}

// ---------------------------------------------------------------------------------------
// Sums

template <typename L, typename R>
  requires LinearOperands<L, R>
HighsLinearExpression operator+(L&& lhs, R&& rhs) {
  HighsLinearExpression sum = internal::toHighsLinear(std::forward<L>(lhs));
  sum += internal::toHighsLinear(std::forward<R>(rhs));
  return sum;
}
template <typename L, typename R>
  requires LinearOperands<L, R>
HighsLinearExpression operator-(L&& lhs, R&& rhs) {
  HighsLinearExpression difference = internal::toHighsLinear(std::forward<L>(lhs));
  difference -= internal::toHighsLinear(std::forward<R>(rhs));
  return difference;
}
template <typename L, typename R>
  requires QuadraticOperands<L, R>
HighsQuadraticExpression operator+(L&& lhs, R&& rhs) {
  HighsQuadraticExpression sum = internal::toHighsQuadratic(std::forward<L>(lhs));
  sum += internal::toHighsQuadratic(std::forward<R>(rhs));
  return sum;
}
template <typename L, typename R>
  requires QuadraticOperands<L, R>
HighsQuadraticExpression operator-(L&& lhs, R&& rhs) {
  HighsQuadraticExpression difference = internal::toHighsQuadratic(std::forward<L>(lhs));
  difference -= internal::toHighsQuadratic(std::forward<R>(rhs));
  return difference;
}

// ---------------------------------------------------------------------------------------
// Rows: comparisons of linear operands. A number on one side gives a row that can take the
// other bound; two expressions give a row with one bound; == fixes both.

template <typename L, typename R>
  requires LinearOperands<L, R>
auto operator<=(L&& lhs, R&& rhs) {
  if constexpr (is_highs_scalar<std::remove_cvref_t<R>>) {
    return HighsUpperBoundedLinearExpression{internal::toHighsLinear(std::forward<L>(lhs)), static_cast<double>(rhs)};
  } else if constexpr (is_highs_scalar<std::remove_cvref_t<L>>) {
    return HighsLowerBoundedLinearExpression{internal::toHighsLinear(std::forward<R>(rhs)), static_cast<double>(lhs)};
  } else {
    return HighsBoundedLinearExpression{std::forward<L>(lhs) - std::forward<R>(rhs), -kHighsInf, 0.0};
  }
}
template <typename L, typename R>
  requires LinearOperands<L, R>
auto operator>=(L&& lhs, R&& rhs) {
  if constexpr (is_highs_scalar<std::remove_cvref_t<R>>) {
    return HighsLowerBoundedLinearExpression{internal::toHighsLinear(std::forward<L>(lhs)), static_cast<double>(rhs)};
  } else if constexpr (is_highs_scalar<std::remove_cvref_t<L>>) {
    return HighsUpperBoundedLinearExpression{internal::toHighsLinear(std::forward<R>(rhs)), static_cast<double>(lhs)};
  } else {
    return HighsBoundedLinearExpression{std::forward<L>(lhs) - std::forward<R>(rhs), 0.0, kHighsInf};
  }
}
template <typename L, typename R>
  requires LinearOperands<L, R>
HighsBoundedLinearExpression operator==(L&& lhs, R&& rhs) {
  return HighsBoundedLinearExpression{std::forward<L>(lhs) - std::forward<R>(rhs), 0.0, 0.0};
}

// A quadratic expression is compared to nothing: HiGHS has no quadratic constraint. The
// templates match exactly, and their bodies stop on one sentence as soon as one is chosen.
template <typename L, typename R>
  requires QuadraticOperands<L, R>
auto operator<=(const L& /*lhs*/, const R& /*rhs*/) {
  static_assert(internal::always_false<L>,
                "HiGHS takes quadratic terms only in the objective: a row is linear. Put the square in "
                "setObjective() or minimize(), or bound an epigraph variable with linear rows.");
  return false;
}
template <typename L, typename R>
  requires QuadraticOperands<L, R>
auto operator>=(const L& /*lhs*/, const R& /*rhs*/) {
  static_assert(internal::always_false<L>,
                "HiGHS takes quadratic terms only in the objective: a row is linear. Put the square in "
                "setObjective() or minimize(), or bound an epigraph variable with linear rows.");
  return false;
}
template <typename L, typename R>
  requires QuadraticOperands<L, R>
auto operator==(const L& /*lhs*/, const R& /*rhs*/) {
  static_assert(internal::always_false<L>,
                "HiGHS takes quadratic terms only in the objective: a row is linear. Put the square in "
                "setObjective() or minimize(), or bound an epigraph variable with linear rows.");
  return false;
}

}  // namespace dreamor::highs

namespace dreamor::internal {

// A HiGHS call's status: an error throws, a warning is HiGHS's to report (it logs it).
inline void highsCall(const char* call, HighsStatus status) {
  if (status == HighsStatus::kError) { throw dreamor::highs::HighsError{call, status}; }
}

// A count for HiGHS, which counts in HighsInt.
[[nodiscard]] inline HighsInt highsCount(std::size_t count) {
  if (count > static_cast<std::size_t>(std::numeric_limits<HighsInt>::max())) {
    throw std::length_error("dreamor::highs: more entries than a HighsInt counts.");
  }
  return static_cast<HighsInt>(count);
}

// The per-thread workspace that merges an expression's repeated columns before HiGHS sees
// them, in time linear in the terms: a column's slot holds its running sum while its stamp
// equals the current pass. Its arrays grow to the widest model the thread has handed over.
class HighsWorkspace {
 public:
  static HighsWorkspace& local() {
    thread_local HighsWorkspace workspace;
    return workspace;
  }

  // Merges (indices, coefficients) over a model of `columns` columns into columns() and
  // values(), in order of first appearance, without the terms that cancel to zero.
  void merge(std::span<const HighsInt> indices, std::span<const double> coefficients, std::size_t columns) {
    beginPass(columns);
    merged_columns_.clear();
    merged_values_.clear();
    for (std::size_t k = 0; k < indices.size(); ++k) {
      const auto column = static_cast<std::size_t>(indices[k]);
      if (stamps_[column] != pass_) {
        stamps_[column] = pass_;
        sums_[column]   = 0.0;
        merged_columns_.push_back(indices[k]);
      }
      sums_[column] += coefficients[k];
    }
    for (const HighsInt column : merged_columns_) {
      merged_values_.push_back(sums_[static_cast<std::size_t>(column)]);
    }
    dropZeros(merged_columns_, merged_values_);
  }

  [[nodiscard]] std::span<const HighsInt> columns() const noexcept { return merged_columns_; }
  [[nodiscard]] std::span<const double> values() const noexcept { return merged_values_; }

  // The objective's costs: every column's, zero where the expression has none.
  [[nodiscard]] std::span<const double> costs(const dreamor::highs::HighsLinearExpression& objective,
                                              std::size_t columns) {
    merge(objective.indices(), objective.coefficients(), columns);
    costs_.assign(columns, 0.0);
    for (std::size_t k = 0; k < merged_columns_.size(); ++k) {
      costs_[static_cast<std::size_t>(merged_columns_[k])] = merged_values_[k];
    }
    return costs_;
  }

  // The Hessian of the quadratic terms, lower triangle, column-wise, as passHessian takes it:
  // HiGHS minimizes c'x + 1/2 x'Qx, so c x_i^2 is Q_ii = 2c and c x_i x_j (i > j) is Q_ij = c.
  // Repeats are merged, zeros dropped, and each column's rows ascend (its diagonal first).
  void hessian(const dreamor::highs::HighsQuadraticExpression& objective, std::size_t columns) {
    const std::span<const HighsInt> rows    = objective.first();
    const std::span<const HighsInt> cols    = objective.second();
    const std::span<const double> quadratic = objective.quadraticCoefficients();
    // Counting sort by column.
    bucket_start_.assign(columns + 1, 0);
    for (const HighsInt column : cols) {
      ++bucket_start_[static_cast<std::size_t>(column) + 1];
    }
    for (std::size_t j = 0; j < columns; ++j) {
      bucket_start_[j + 1] += bucket_start_[j];
    }
    bucket_rows_.resize(rows.size());
    bucket_values_.resize(rows.size());
    fill_.assign(bucket_start_.begin(), bucket_start_.end() - 1);
    for (std::size_t k = 0; k < rows.size(); ++k) {
      const std::size_t at = fill_[static_cast<std::size_t>(cols[k])]++;
      bucket_rows_[at]     = rows[k];
      bucket_values_[at]   = quadratic[k];
    }
    // Each column merged by row, sorted, scaled.
    hessian_start_.assign(1, 0);
    hessian_index_.clear();
    hessian_value_.clear();
    for (std::size_t j = 0; j < columns; ++j) {
      const std::size_t from = bucket_start_[j];
      const std::size_t to   = bucket_start_[j + 1];
      merge(std::span<const HighsInt>{bucket_rows_}.subspan(from, to - from),
            std::span<const double>{bucket_values_}.subspan(from, to - from),
            columns);
      sortMergedByIndex();
      for (std::size_t k = 0; k < merged_columns_.size(); ++k) {
        const bool diagonal = std::cmp_equal(merged_columns_[k], j);
        hessian_index_.push_back(merged_columns_[k]);
        hessian_value_.push_back(diagonal ? kDiagonalScale * merged_values_[k] : merged_values_[k]);
      }
      hessian_start_.push_back(highsCount(hessian_index_.size()));
    }
  }

  [[nodiscard]] std::span<const HighsInt> hessianStart() const noexcept { return hessian_start_; }
  [[nodiscard]] std::span<const HighsInt> hessianIndex() const noexcept { return hessian_index_; }
  [[nodiscard]] std::span<const double> hessianValue() const noexcept { return hessian_value_; }

  // An empty Hessian over `columns` columns: what clears a quadratic objective.
  [[nodiscard]] std::span<const HighsInt> emptyHessianStart(std::size_t columns) {
    hessian_start_.assign(columns + 1, 0);
    return hessian_start_;
  }

 private:
  void beginPass(std::size_t columns) {
    if (sums_.size() < columns) {
      sums_.resize(columns, 0.0);
      stamps_.resize(columns, 0);
    }
    if (++pass_ == 0) {  // wrapped around: no slot may look current
      std::ranges::fill(stamps_, 0U);
      pass_ = 1;
    }
  }

  static void dropZeros(std::vector<HighsInt>& columns, std::vector<double>& values) {
    std::size_t kept = 0;
    for (std::size_t k = 0; k < columns.size(); ++k) {
      if (std::fpclassify(values[k]) != FP_ZERO) {
        columns[kept] = columns[k];
        values[kept]  = values[k];
        ++kept;
      }
    }
    columns.resize(kept);
    values.resize(kept);
  }

  // The merged entries by ascending index (a Hessian column's rows): few, so an insertion sort.
  void sortMergedByIndex() {
    for (std::size_t k = 1; k < merged_columns_.size(); ++k) {
      const HighsInt column = merged_columns_[k];
      const double value    = merged_values_[k];
      std::size_t at        = k;
      while (at > 0 && merged_columns_[at - 1] > column) {
        merged_columns_[at] = merged_columns_[at - 1];
        merged_values_[at]  = merged_values_[at - 1];
        --at;
      }
      merged_columns_[at] = column;
      merged_values_[at]  = value;
    }
  }

  // c x_i^2 is Q_ii = 2c: HiGHS's objective halves x'Qx.
  static constexpr double kDiagonalScale = 2.0;

  std::vector<double> sums_;
  std::vector<std::uint32_t> stamps_;
  std::uint32_t pass_{0};
  std::vector<HighsInt> merged_columns_;
  std::vector<double> merged_values_;
  std::vector<double> costs_;
  std::vector<std::size_t> bucket_start_;
  std::vector<std::size_t> fill_;
  std::vector<HighsInt> bucket_rows_;
  std::vector<double> bucket_values_;
  std::vector<HighsInt> hessian_start_;
  std::vector<HighsInt> hessian_index_;
  std::vector<double> hessian_value_;
};

// The environment an expression's variables belong to must be the one it is handed to.
inline void checkHighsModel([[maybe_unused]] const dreamor::highs::HighsEnv* expression_env,
                            [[maybe_unused]] const dreamor::highs::HighsEnv& env,
                            [[maybe_unused]] const char* call) {
#ifndef NDEBUG
  if (expression_env != nullptr && expression_env != &env) {
    DREAMOR_DEBUG_FATAL(
        "dreamor::highs::{}: the expression's variables belong to another HighsEnv - a broken contract "
        "(dreamor/highs.hpp).",
        call);
  }
#endif
}

}  // namespace dreamor::internal

namespace dreamor::highs {

// A model as dreamor writes it, and the Highs that solves it: Concert's IloEnv, with the model and the
// algorithm one object, as they are in HiGHS. What VarTable, addConstr and setObjective write is collected
// in a HighsModel, and the Highs gets all of it in one passModel the first time it is reached - highs(),
// a solution(), minimize(). From then on each goes to the Highs as it is written, as Concert's model
// reaches an IloCplex that has extracted it. The Highs is reached through highs() alone, and is never
// behind what was written there, so its own API - options, run, getLp, writeModel, changeColBounds - is
// used as it is.
//
// The one passModel is the point: a Highs call per row costs more than the row - HiGHS clears what it
// derived from its model at every change (about 0.6 us a row) and keeps its name index current. So build
// first and reach the Highs after: reaching it first leaves every row to the incremental path, and nothing
// is lost but time. Options touch no model, and HiGHS takes them as a HighsOptions: given to the
// constructor, they are the Highs's before anything reaches it - a quiet log for the passModel too. A
// model HiGHS refuses throws HighsError from that first reach ("passModel").
//
//     HighsOptions options;
//     options.output_flag = false;
//     options.time_limit  = 60;
//     hs::HighsEnv env{options};
//     const hs::VarTable<MachineCol, ItemCol, PeriodCol> vYield{env, sPRODUCE, 0, max_possible_qty};
//     hs::addConstr(env, hs::sumOf(rows, machine_usage).build() <= capacity);
//     hs::minimize(env, hs::sumOf(sPRODUCE, unit_cost).build());   // the model reaches the Highs here
class HighsEnv {
 public:
  HighsEnv() {
    model_.lp_.a_matrix_.format_ = MatrixFormat::kRowwise;
    model_.lp_.a_matrix_.start_.assign(1, 0);
  }
  // The Highs takes these options at once (passOptions): they touch no model, so nothing is passed.
  explicit HighsEnv(const HighsOptions& options) : HighsEnv{} {
    internal::highsCall("passOptions", highs_.passOptions(options));
  }
  // Variables point at their environment: it stays where it is.
  HighsEnv(const HighsEnv&)            = delete;
  HighsEnv& operator=(const HighsEnv&) = delete;
  HighsEnv(HighsEnv&&)                 = delete;
  HighsEnv& operator=(HighsEnv&&)      = delete;
  ~HighsEnv()                          = default;

  // The Highs, with everything written so far in it.
  [[nodiscard]] Highs& highs() {
    pass();
    return highs_;
  }
  [[nodiscard]] const Highs& highs() const {
    pass();
    return highs_;
  }

 private:
  friend struct internal::HighsEnvAccess;

  // Hands the collected model over, once; the model is empty after. pass() is const - a const
  // environment's Highs is reached too - so what it moves is mutable.
  void pass() const {
    if (passed_) { return; }
    passed_     = true;
    HighsLp& lp = model_.lp_;
    if (lp.col_cost_.empty() && lp.row_lower_.empty()) {
      if (lp.sense_ != ObjSense::kMinimize) {
        internal::highsCall("changeObjectiveSense", highs_.changeObjectiveSense(lp.sense_));
      }
      if (std::fpclassify(lp.offset_) != FP_ZERO) {
        internal::highsCall("changeObjectiveOffset", highs_.changeObjectiveOffset(lp.offset_));
      }
      return;
    }
    lp.num_col_           = internal::highsCount(lp.col_cost_.size());
    lp.num_row_           = internal::highsCount(lp.row_lower_.size());
    lp.a_matrix_.num_col_ = lp.num_col_;
    lp.a_matrix_.num_row_ = lp.num_row_;
    if (!named_) {
      lp.col_names_.clear();
      lp.row_names_.clear();
    }
    // Columns added after a quadratic objective have no squares: the Hessian reaches them empty.
    HighsHessian& hessian = model_.hessian_;
    if (hessian.dim_ > 0 && hessian.dim_ < lp.num_col_) {
      hessian.start_.resize(static_cast<std::size_t>(lp.num_col_) + 1, hessian.start_.back());
      hessian.dim_ = lp.num_col_;
    }
    HighsModel model = std::move(model_);
    model_           = HighsModel{};
    internal::highsCall("passModel", highs_.passModel(std::move(model)));
  }

  mutable HighsModel model_;  // until passed: what was written, its matrix row-wise
  mutable Highs highs_;
  mutable bool passed_{false};
  bool named_{false};  // a column or row of model_ has a name
};

}  // namespace dreamor::highs

namespace dreamor::internal {

// What VarTable, addConstr and setObjective write, into the collected model or - once it is passed -
// into the Highs, as the environment stands.
struct HighsEnvAccess {
  using HighsEnv = dreamor::highs::HighsEnv;

  [[nodiscard]] static HighsInt numCol(const HighsEnv& env) noexcept {
    return env.passed_ ? env.highs_.getNumCol() : static_cast<HighsInt>(env.model_.lp_.col_cost_.size());
  }

  // Columns with these bounds and one type; returns the first one's index.
  static HighsInt addColumns(HighsEnv& env,
                             std::span<const double> lower,
                             std::span<const double> upper,
                             HighsVarType type) {
    const HighsInt first    = numCol(env);
    const std::size_t count = lower.size();
    if (env.passed_) {
      highsCall("addVars", env.highs_.addVars(highsCount(count), lower.data(), upper.data()));
      if (type != HighsVarType::kContinuous) {
        const std::vector<HighsVarType> types(count, type);
        const HighsInt last = highsCount(static_cast<std::size_t>(first) + count) - 1;
        highsCall("changeColsIntegrality", env.highs_.changeColsIntegrality(first, last, types.data()));
      }
      return first;
    }
    HighsLp& lp = env.model_.lp_;
    lp.col_lower_.insert(lp.col_lower_.end(), lower.begin(), lower.end());
    lp.col_upper_.insert(lp.col_upper_.end(), upper.begin(), upper.end());
    lp.col_cost_.resize(lp.col_cost_.size() + count, 0.0);
    lp.integrality_.resize(lp.integrality_.size() + count, type);
    lp.col_names_.resize(lp.col_names_.size() + count);
    return first;
  }

  static void nameColumn(HighsEnv& env, HighsInt column, const char* name) {
    if (env.passed_) {
      highsCall("passColName", env.highs_.passColName(column, name));
      return;
    }
    env.model_.lp_.col_names_[static_cast<std::size_t>(column)] = name;
    env.named_                                                  = true;
  }

  // A row of merged columns; returns its index.
  static HighsInt addRow(
      HighsEnv& env, double lower, double upper, std::span<const HighsInt> columns, std::span<const double> values) {
    if (env.passed_) {
      highsCall("addRow", env.highs_.addRow(lower, upper, highsCount(columns.size()), columns.data(), values.data()));
      return env.highs_.getNumRow() - 1;
    }
    HighsLp& lp             = env.model_.lp_;
    HighsSparseMatrix& rows = lp.a_matrix_;
    rows.index_.insert(rows.index_.end(), columns.begin(), columns.end());
    rows.value_.insert(rows.value_.end(), values.begin(), values.end());
    rows.start_.push_back(highsCount(rows.index_.size()));
    lp.row_lower_.push_back(lower);
    lp.row_upper_.push_back(upper);
    lp.row_names_.emplace_back();
    return highsCount(lp.row_lower_.size()) - 1;
  }

  static void nameRow(HighsEnv& env, HighsInt row, const char* name) {
    if (env.passed_) {
      highsCall("passRowName", env.highs_.passRowName(row, name));
      return;
    }
    env.model_.lp_.row_names_[static_cast<std::size_t>(row)] = name;
    env.named_                                               = true;
  }

  // The objective: every column's cost (zero where it has none), the constant, and the Hessian - built
  // from `squares`, or cleared when there are none.
  static void setObjective(HighsEnv& env,
                           const dreamor::highs::HighsLinearExpression& linear,
                           const dreamor::highs::HighsQuadraticExpression* squares) {
    checkHighsModel(HighsModelAccess::of(linear), env, "setObjective");
    const auto columns        = static_cast<std::size_t>(numCol(env));
    HighsWorkspace& workspace = HighsWorkspace::local();
    if (!env.passed_) {
      HighsModel& model = env.model_;
      if (columns > 0) {
        const std::span<const double> costs = workspace.costs(linear, columns);
        model.lp_.col_cost_.assign(costs.begin(), costs.end());
      }
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

}  // namespace dreamor::internal

namespace dreamor::highs {

template <typename... Keys>
  requires(sizeof...(Keys) > 0 && (internal::is_nameable<Keys> && ...))
class VarTable : public internal::VarTableCore<HighsVar, Keys...> {
  using Core = internal::VarTableCore<HighsVar, Keys...>;

 public:
  using Key = Core::Key;
  // Lookup, contains(), size(), keys(), iteration and the constructor that stops on a set with
  // other key columns are the core's, as on Concert. Copy is deleted (a HighsVar names a column
  // of the model: a copy would alias it under two names) and move is fine; the columns belong
  // to the model, not to this table.
  using Core::Core;

  // The set's rows become columns of the environment's model, in the set's order - collected, or,
  // once the Highs has the model, one addVars and one changeColsIntegrality over the range unless the
  // type is continuous. The type is HiGHS's own, as highspy's addVariable takes it: kContinuous,
  // kInteger, kSemiContinuous, kSemiInteger.
  template <typename Lb = double, typename Ub = double>
  VarTable(HighsEnv& env,
           const IndexSet<Key>& set,
           const Lb& lb      = 0,
           const Ub& ub      = kHighsInf,
           HighsVarType type = HighsVarType::kContinuous,
           const char* name  = nullptr)
      : Core{set} {
    internal::checkBound<Lb, Key>();
    internal::checkBound<Ub, Key>();
    const std::size_t count = this->rows().size();
    if (count == 0) { return; }
    std::vector<double> lower;
    std::vector<double> upper;
    lower.reserve(count);
    upper.reserve(count);
    for (const Key& row : this->rows()) {
      lower.push_back(internal::boundOf(lb, row));
      upper.push_back(internal::boundOf(ub, row));
    }
    const HighsInt first = internal::HighsEnvAccess::addColumns(env, lower, upper, type);
    typename Core::NameBuilder names{name};
    this->vars_.reserve(count);
    for (const Key& row : this->rows()) {
      const HighsVar variable{first + internal::highsCount(this->vars_.size()), env};
      if (const char* const variable_name = Core::nameOf(names, row); variable_name != nullptr) {
        internal::HighsEnvAccess::nameColumn(env, variable.index(), variable_name);
      }
      this->vars_.push_back(variable);
    }
  }

  // Every value of the model's primal solution, keyed like this table. The result owns plain
  // doubles, so it outlives the model. Throws std::logic_error when the model has no primal
  // solution (HiGHS would hand back an empty or stale col_value).
  [[nodiscard]] ValueTable<Keys...> solution(const HighsEnv& env) const {
    const HighsSolution& solved = env.highs().getSolution();
    if (!solved.value_valid) {
      throw std::logic_error(
          "VarTable::solution: the model has no primal solution - run() it, and check "
          "its model status.");
    }
    std::vector<double> values;
    values.reserve(this->vars_.size());
    for (const HighsVar& variable : this->vars_) {
      internal::checkHighsModel(&variable.env(), env, "VarTable::solution");
      values.push_back(solved.col_value[static_cast<std::size_t>(variable.index())]);
    }
    return this->valueTable(std::move(values));
  }
};

// Adds a row - an expression with its bounds, x + y <= 1 or (0 <= x - y) <= 5 - and returns it,
// as highspy's addConstr. Its repeated columns are merged first: HiGHS refuses a row that names
// a column twice.
template <typename Row>
HighsCons addConstr(HighsEnv& env, Row&& row, const char* name = nullptr) {
  using Kind = std::remove_cvref_t<Row>;
  if constexpr (is_highs_row<Kind>) {
    const HighsBoundedLinearExpression bounded{std::forward<Row>(row)};
    internal::checkHighsModel(internal::HighsModelAccess::of(bounded.expression()), env, "addConstr");
    internal::HighsWorkspace& workspace = internal::HighsWorkspace::local();
    workspace.merge(bounded.expression().indices(),
                    bounded.expression().coefficients(),
                    static_cast<std::size_t>(internal::HighsEnvAccess::numCol(env)));
    const HighsCons added{internal::HighsEnvAccess::addRow(
                              env, bounded.lower(), bounded.upper(), workspace.columns(), workspace.values()),
                          env};
    if (name != nullptr) { internal::HighsEnvAccess::nameRow(env, added.index(), name); }
    return added;
  } else {
    static_assert(internal::always_false<Kind>,
                  "addConstr takes a row - an expression with its bounds: addConstr(env, x + y <= 1), or "
                  "addConstr(env, (0 <= x - y) <= 5).");
    return HighsCons{0, env};
  }
}

// Replaces the objective - its costs, constant and Hessian - as highspy's setObjective does,
// and the sense when one is given. A linear objective clears a Hessian left from before.
template <typename Objective>
void setObjective(HighsEnv& env, const Objective& objective) {
  using Kind = std::remove_cvref_t<Objective>;
  if constexpr (is_highs_row<Kind>) {
    static_assert(internal::always_false<Kind>,
                  "An objective is an expression, not a row: setObjective(env, x + y), without the bound.");
  } else if constexpr (std::same_as<Kind, HighsQuadraticExpression>) {
    internal::HighsEnvAccess::setObjective(env, objective.linear(), &objective);
  } else if constexpr (std::same_as<Kind, HighsQuadraticTerm>) {
    const HighsQuadraticExpression quadratic{objective};
    internal::HighsEnvAccess::setObjective(env, quadratic.linear(), &quadratic);
  } else if constexpr (is_highs_linear<Kind>) {
    internal::HighsEnvAccess::setObjective(env, internal::toHighsLinear(objective), nullptr);
  } else {
    static_assert(internal::always_false<Kind>,
                  "setObjective takes an expression of this backend's variables: a HighsVar, a term, a "
                  "HighsLinearExpression or a HighsQuadraticExpression.");
  }
}
template <typename Objective>
void setObjective(HighsEnv& env, const Objective& objective, ObjSense sense) {
  setObjective(env, objective);
  internal::HighsEnvAccess::setSense(env, sense);
}

// setObjective, then run(), as highspy's minimize and maximize: the status is run()'s.
template <typename Objective>
HighsStatus minimize(HighsEnv& env, const Objective& objective) {
  setObjective(env, objective, ObjSense::kMinimize);
  return env.highs().run();
}
template <typename Objective>
HighsStatus maximize(HighsEnv& env, const Objective& objective) {
  setObjective(env, objective, ObjSense::kMaximize);
  return env.highs().run();
}

// sum {t in rows} term(t) as a HighsLinearExpression, or a HighsQuadraticExpression when the
// term is quadratic (x * x, (x - 1) * (x - 1)); see concert.hpp for where(), split() and the
// rules on rows and callables, which are the same here.
template <typename... Columns, std::ranges::input_range Rows, typename Term>
[[nodiscard]] auto sumOf(Rows&& rows, Term&& term) {
  return internal::makeSum<Columns...>(
      [] { return HighsLinearExpression{}; }, std::forward<Rows>(rows), std::forward<Term>(term));
}

// sumOf(highs, rows, term) - with a first argument, as VarTable and Concert's sumOf take one -
// stops on the fix.
template <typename... Columns, typename First, typename Rows, typename Term>
auto sumOf(First&& /*first*/, Rows&& /*rows*/, Term&& /*term*/) {
  static_assert(internal::always_false<First>,
                "HiGHS's sumOf takes no HighsEnv: sumOf(rows, term). An empty HighsLinearExpression needs no model.");
}

}  // namespace dreamor::highs

// A quadratic term makes the sum a HighsQuadraticExpression; a linear one keeps it linear.
template <typename Term>
  requires(dreamor::highs::is_highs_quadratic<Term>)
struct dreamor::internal::SumTotal<dreamor::highs::HighsLinearExpression, Term> {
  using type = dreamor::highs::HighsQuadraticExpression;
};
