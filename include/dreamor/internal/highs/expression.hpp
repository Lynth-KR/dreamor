// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The HiGHS backend's expressions: variables, terms, linear and quadratic expressions, and rows.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/backend/inline_vector.hpp"
#include "dreamor/internal/index_set/column.hpp"
#include "dreamor/logger.hpp"

#include "dreamor/internal/base/third_party_headers.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#include "Highs.h"
DREAMOR_END_THIRD_PARTY_HEADERS

// The message for every call that puts a quadratic term into a row.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_QUADRATIC_ROW_MESSAGE                                                               \
  "HiGHS takes quadratic terms only in the objective: a row is linear. Put the square in setObjective() or " \
  "minimize()."

namespace dreamor::highs::detail {
struct HighsModelAccess;
}  // namespace dreamor::highs::detail

namespace dreamor::highs {
class HighsEnv;
}  // namespace dreamor::highs

namespace dreamor::highs {

/// @section dreamor::highs

/// A variable of a HiGHS model: the index of a column and the `HighsEnv` that the column belongs to.
///
/// `HighsEnv::addVariable` and the constructor of a `VarTable` create variables. A variable refers to its `HighsEnv`
/// and does not own it. A `HighsVar` corresponds to highspy's `highs_var`.
class HighsVar {
 public:
  /// Constructs the variable of the column `index` in the model of `env`.
  ///
  /// dreamor does not check `index`. If `index` is not the index of a column in the model of `env`, the behavior is
  /// undefined.
  ///
  /// Takes: `index` must be the index of a column in the model of `env`.
  HighsVar(HighsInt index, const HighsEnv& env) noexcept : index_{index}, env_{&env} {}

  /// Returns the index of the variable's column in the model.
  [[nodiscard]] HighsInt index() const noexcept { return index_; }

  /// Returns the `HighsEnv` that the variable belongs to.
  [[nodiscard]] const HighsEnv& env() const noexcept { return *env_; }

 private:
  HighsInt index_;
  const HighsEnv* env_;
};

/// A constraint of a HiGHS model: the index of a row and the `HighsEnv` that the row belongs to.
///
/// `addConstr` returns a `HighsCons`. A `HighsCons` corresponds to highspy's `highs_cons`.
class HighsCons {
 public:
  /// Constructs the constraint of the row `index` in the model of `env`.
  ///
  /// dreamor does not check `index`. If `index` is not the index of a row in the model of `env`, the behavior is
  /// undefined.
  ///
  /// Takes: `index` must be the index of a row in the model of `env`.
  HighsCons(HighsInt index, const HighsEnv& env) noexcept : index_{index}, env_{&env} {}

  /// Returns the index of the constraint's row in the model.
  [[nodiscard]] HighsInt index() const noexcept { return index_; }

  /// Returns the `HighsEnv` that the constraint belongs to.
  [[nodiscard]] const HighsEnv& env() const noexcept { return *env_; }

 private:
  HighsInt index_;
  const HighsEnv* env_;
};

/// A coefficient times one variable.
///
/// A `HighsVar` multiplied or divided by a number is a `HighsLinearTerm`. A term is a plain value: it allocates no
/// memory.
struct HighsLinearTerm {
  /// The variable.
  HighsVar variable;
  /// The coefficient of the variable.
  double coefficient;
};

/// A coefficient times the product of two variables.
///
/// The product of two variables, of two `HighsLinearTerm`s, or of a variable and a `HighsLinearTerm` is a
/// `HighsQuadraticTerm`. The two variables may be the same variable.
struct HighsQuadraticTerm {
  /// The first variable.
  HighsVar first;
  /// The second variable.
  HighsVar second;
  /// The coefficient of the product.
  double coefficient;
};

class HighsQuadraticExpression;

/// A sum of terms, each a coefficient times a variable, plus a constant.
///
/// An expression keeps its terms in the order in which they are appended. A variable that is appended twice makes
/// two terms, and the expression does not merge them. When `addConstr` or `setObjective` passes the expression to the
/// model, the terms over the same column are added together. A column whose coefficients add up to zero is left out.
///
/// A default-constructed expression has no terms, and its constant is 0. A number, a `HighsVar` and a
/// `HighsLinearTerm` convert implicitly to an expression.
///
/// A `HighsLinearExpression` corresponds to highspy's `highs_linear_expression` without bounds. In dreamor, an
/// expression with bounds is a row, which has types of its own.
///
/// Contract: highs_one_environment
/// Cost: An expression stores its first few terms inside the object, four on a 64-bit platform. It allocates memory
/// only when it has more terms.
class HighsLinearExpression {
 public:
  HighsLinearExpression() = default;

  // NOLINTBEGIN(*-explicit-constructor): a number, a variable and a term are expressions, as in highspy
  /// Constructs an expression that has no terms and the constant `constant`.
  ///
  /// The constructor is implicit, so a number converts to an expression.
  HighsLinearExpression(double constant) noexcept : constant_{constant} {}

  /// @overload
  /// Constructs an expression of one term, `variable` with the coefficient 1, and the constant 0.
  ///
  /// The constructor is implicit, so a variable converts to an expression.
  HighsLinearExpression(HighsVar variable) { append(variable, 1.0); }

  /// @overload
  /// Constructs an expression of the one term `term` and the constant 0.
  ///
  /// The constructor is implicit, so a term converts to an expression.
  HighsLinearExpression(const HighsLinearTerm& term) { append(term.variable, term.coefficient); }

  // NOLINTEND(*-explicit-constructor)

  /// Appends a variable or a term to the expression, or adds a constant to its constant.
  ///
  /// A variable is appended as a term with the coefficient 1.
  ///
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsLinearExpression& operator+=(HighsVar variable) {
    append(variable, 1.0);
    return *this;
  }

  /// @overload
  HighsLinearExpression& operator+=(const HighsLinearTerm& term) {
    append(term.variable, term.coefficient);
    return *this;
  }

  /// @overload
  HighsLinearExpression& operator+=(double constant) noexcept {
    constant_ += constant;
    return *this;
  }

  /// @overload
  /// Appends the terms of `other` to the expression, and adds the constant of `other` to its constant.
  ///
  /// If `other` is this expression itself, the expression then holds each of its terms twice, and its constant is
  /// doubled.
  ///
  /// Takes: `other` may be this expression itself.
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsLinearExpression& operator+=(const HighsLinearExpression& other) {
    appendAll(other, 1.0);
    constant_ += other.constant_;
    return *this;
  }

  /// @overload
  /// Appends the terms of the rvalue `other` to the expression, and adds the constant of `other` to its constant.
  ///
  /// If the expression has no terms, it takes over the storage of `other` and does not copy the terms. `other` is
  /// then left in a valid but unspecified state.
  ///
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsLinearExpression& operator+=(HighsLinearExpression&& other) {
    if (indices_.empty()) {  // Take the storage of other instead of copying into empty storage.
      const double constant = constant_;
      // An expression with no terms can still hold an environment: the linear part of a quadratic expression does.
      // It is checked against the environment of other after the move, as append() checks a term's.
      const HighsEnv* const env = env_;
      *this                     = std::move(other);
      constant_ += constant;
      adopt(env);
      return *this;
    }
    return *this += static_cast<const HighsLinearExpression&>(other);
  }

  /// Appends a variable or a term with the opposite sign to the expression, or subtracts a constant from its
  /// constant.
  ///
  /// A variable is appended as a term with the coefficient -1.
  ///
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsLinearExpression& operator-=(HighsVar variable) {
    append(variable, -1.0);
    return *this;
  }

  /// @overload
  HighsLinearExpression& operator-=(const HighsLinearTerm& term) {
    append(term.variable, -term.coefficient);
    return *this;
  }

  /// @overload
  HighsLinearExpression& operator-=(double constant) noexcept {
    constant_ -= constant;
    return *this;
  }

  /// @overload
  /// Appends the terms of `other` with their coefficients negated, and subtracts the constant of `other` from the
  /// constant of the expression.
  ///
  /// If `other` is this expression itself, the expression then holds each of its terms once with each sign, and its
  /// constant becomes 0.
  ///
  /// Takes: `other` may be this expression itself.
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsLinearExpression& operator-=(const HighsLinearExpression& other) {
    appendAll(other, -1.0);
    constant_ -= other.constant_;
    return *this;
  }

  /// Multiplies every coefficient and the constant by `factor`.
  ///
  /// Returns: A reference to this expression.
  HighsLinearExpression& operator*=(double factor) noexcept {
    for (double& coefficient : coefficients_) {
      coefficient *= factor;
    }
    constant_ *= factor;
    return *this;
  }

  /// Multiplies every coefficient and the constant by `1 / divisor`.
  ///
  /// Returns: A reference to this expression.
  HighsLinearExpression& operator/=(double divisor) noexcept { return *this *= 1.0 / divisor; }

  /// Returns the column index of each term, in the order in which the terms were appended.
  ///
  /// A column appears once for each term over it. The span is valid until the expression changes or is destroyed.
  [[nodiscard]] std::span<const HighsInt> indices() const noexcept { return indices_; }

  /// Returns the coefficient of each term, in the same order as `indices()`.
  ///
  /// The span is valid until the expression changes or is destroyed.
  [[nodiscard]] std::span<const double> coefficients() const noexcept { return coefficients_; }

  /// Returns the constant of the expression.
  [[nodiscard]] double constant() const noexcept { return constant_; }

  /// Returns the number of terms.
  ///
  /// Terms over the same column are counted separately.
  [[nodiscard]] std::size_t size() const noexcept { return indices_.size(); }

 private:
  friend class HighsQuadraticExpression;
  friend struct detail::HighsModelAccess;

  // Appends every term of other, multiplied by sign. The storage is reserved first and the terms are read by index,
  // so other may be this expression itself: nothing reallocates while the terms are read.
  void appendAll(const HighsLinearExpression& other, double sign) {
    adopt(other.env_);
    const std::size_t count = other.indices_.size();
    indices_.reserve(indices_.size() + count);
    coefficients_.reserve(coefficients_.size() + count);
    for (std::size_t k = 0; k < count; ++k) {
      indices_.pushBack(other.indices_[k]);
      coefficients_.pushBack(sign * other.coefficients_[k]);
    }
  }

  void append(HighsVar variable, double coefficient) {
    adopt(&variable.env());
    indices_.pushBack(variable.index());
    coefficients_.pushBack(coefficient);
  }

  /// @contract highs_one_environment: The variables of one expression belong to one `HighsEnv`.

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

  // Most rows of a model have a few terms, such as two linked variables or a bound on a short sum, and the operators
  // that write a row make one or two expressions on the way. Terms kept inside the expression cost no allocation. A
  // longer sum moves to the heap, as a std::vector would. The number of inline terms follows from the layout, as in
  // a short string: the inline arrays take the space of the two vectors that they replace, which is 48 / 12 = 4
  // terms on a 64-bit platform. The exact number matters little. What matters is that a short expression allocates
  // at most once, and not once for each growth of each of two vectors.
  static constexpr std::size_t kInlineTerms =
      (sizeof(std::vector<HighsInt>) + sizeof(std::vector<double>)) / (sizeof(HighsInt) + sizeof(double));

  internal::InlineVector<HighsInt, kInlineTerms> indices_;
  internal::InlineVector<double, kInlineTerms> coefficients_;
  double constant_{0.0};
  const HighsEnv* env_{nullptr};
};

/// A linear expression plus a sum of quadratic terms, each a coefficient times the product of two variables.
///
/// HiGHS accepts quadratic terms only in the objective. `setObjective`, `minimize` and `maximize` accept a quadratic
/// expression, and `addConstr` does not.
///
/// A quadratic expression keeps its quadratic terms in the order in which they are appended, and does not merge
/// them. When `setObjective` passes the expression to the model, the terms over the same two columns are added
/// together, whichever column comes first in a term. A `HighsLinearExpression` and a `HighsQuadraticTerm` convert
/// implicitly to a quadratic expression.
///
/// Contract: highs_one_environment
class HighsQuadraticExpression {
 public:
  HighsQuadraticExpression() = default;

  // NOLINTBEGIN(*-explicit-constructor): a linear expression and a quadratic term are quadratic expressions
  /// Constructs a quadratic expression whose linear part is `linear` and that has no quadratic terms.
  ///
  /// The constructor is implicit, so a linear expression converts to a quadratic expression.
  HighsQuadraticExpression(HighsLinearExpression linear) : linear_{std::move(linear)} {}

  /// @overload
  /// Constructs a quadratic expression of the one quadratic term `term`, with an empty linear part.
  ///
  /// The constructor is implicit, so a quadratic term converts to a quadratic expression.
  HighsQuadraticExpression(const HighsQuadraticTerm& term) { append(term); }

  // NOLINTEND(*-explicit-constructor)

  /// Appends the quadratic term `term` to the expression.
  ///
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsQuadraticExpression& operator+=(const HighsQuadraticTerm& term) {
    append(term);
    return *this;
  }

  /// @overload
  /// Appends the quadratic terms of `other` to the expression, and adds the linear part of `other` to its linear
  /// part.
  ///
  /// Takes: `other` may be this expression itself.
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsQuadraticExpression& operator+=(const HighsQuadraticExpression& other) {
    appendAll(other, 1.0);
    return *this;
  }

  /// @overload
  /// Adds `linear` to the linear part of the expression.
  ///
  /// Takes: `linear` must be a linear operand: a number, a `HighsVar`, a `HighsLinearTerm` or a
  /// `HighsLinearExpression`.
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  template <typename Linear>
    requires std::constructible_from<HighsLinearExpression, Linear&&>
  HighsQuadraticExpression& operator+=(Linear&& linear) {
    linear_ += std::forward<Linear>(linear);
    return *this;
  }

  /// Appends the quadratic term `term` with the opposite sign to the expression.
  ///
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsQuadraticExpression& operator-=(const HighsQuadraticTerm& term) {
    append(HighsQuadraticTerm{.first = term.first, .second = term.second, .coefficient = -term.coefficient});
    return *this;
  }

  /// @overload
  /// Appends the quadratic terms of `other` with their coefficients negated, and subtracts the linear part of `other`
  /// from the linear part of the expression.
  ///
  /// Takes: `other` may be this expression itself.
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  HighsQuadraticExpression& operator-=(const HighsQuadraticExpression& other) {
    appendAll(other, -1.0);
    return *this;
  }

  /// @overload
  /// Subtracts `linear` from the linear part of the expression.
  ///
  /// Takes: `linear` must be a linear operand: a number, a `HighsVar`, a `HighsLinearTerm` or a
  /// `HighsLinearExpression`.
  /// Returns: A reference to this expression.
  /// Contract: highs_one_environment
  template <typename Linear>
    requires std::constructible_from<HighsLinearExpression, Linear&&>
  HighsQuadraticExpression& operator-=(Linear&& linear) {
    linear_ -= HighsLinearExpression{std::forward<Linear>(linear)};
    return *this;
  }

  /// Multiplies every coefficient, linear and quadratic, and the constant by `factor`.
  ///
  /// Returns: A reference to this expression.
  HighsQuadraticExpression& operator*=(double factor) noexcept {
    linear_ *= factor;
    for (double& coefficient : quadratic_coefficients_) {
      coefficient *= factor;
    }
    return *this;
  }

  /// Multiplies every coefficient, linear and quadratic, and the constant by `1 / divisor`.
  ///
  /// Returns: A reference to this expression.
  HighsQuadraticExpression& operator/=(double divisor) noexcept { return *this *= 1.0 / divisor; }

  /// Returns the linear part of the expression, including the constant.
  [[nodiscard]] const HighsLinearExpression& linear() const noexcept { return linear_; }

  /// Returns, for each quadratic term, the larger of its two column indices, in the order in which the terms were
  /// appended.
  ///
  /// A term's column in `first()` is never less than its column in `second()`. Each term is therefore an entry in
  /// the lower triangle of a symmetric matrix. The span is valid until the expression changes or is destroyed.
  [[nodiscard]] std::span<const HighsInt> first() const noexcept { return first_; }

  /// Returns, for each quadratic term, the smaller of its two column indices, in the same order as `first()`.
  ///
  /// The span is valid until the expression changes or is destroyed.
  [[nodiscard]] std::span<const HighsInt> second() const noexcept { return second_; }

  /// Returns the coefficient of each quadratic term, in the same order as `first()`.
  ///
  /// The span is valid until the expression changes or is destroyed.
  [[nodiscard]] std::span<const double> quadraticCoefficients() const noexcept { return quadratic_coefficients_; }

 private:
  friend struct detail::HighsModelAccess;

  // As in HighsLinearExpression::appendAll, other may be this expression itself.
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

/// A row: a linear expression between a lower and an upper bound.
///
/// `addConstr` adds a row to the model. A call that passes a row to `setObjective` does not compile. A bound of
/// `-kHighsInf` or `kHighsInf` means that the row has no bound on that side.
///
/// The comparison operators `<=`, `>=` and `==` on linear operands return a row, or a row with one bound that
/// converts to a `HighsBoundedLinearExpression`.
class HighsBoundedLinearExpression {
 public:
  /// Constructs the row `lower <= expression <= upper`.
  ///
  /// The constructor moves the constant of `expression` into the bounds. The constant is subtracted from `lower`,
  /// from `upper` and from the expression, so the expression of the row has the constant 0.
  HighsBoundedLinearExpression(HighsLinearExpression expression, double lower, double upper)
      : expression_{std::move(expression)}, lower_{lower}, upper_{upper} {
    const double constant = expression_.constant();
    expression_ -= constant;
    lower_ -= constant;
    upper_ -= constant;
  }

  /// Returns the expression of the row.
  ///
  /// Returns: An expression whose constant is 0.
  [[nodiscard]] const HighsLinearExpression& expression() const noexcept { return expression_; }

  /// Returns the lower bound of the row.
  ///
  /// Returns: `-kHighsInf` if the row has no lower bound.
  [[nodiscard]] double lower() const noexcept { return lower_; }

  /// Returns the upper bound of the row.
  ///
  /// Returns: `kHighsInf` if the row has no upper bound.
  [[nodiscard]] double upper() const noexcept { return upper_; }

 private:
  HighsLinearExpression expression_;
  double lower_;
  double upper_;
};

/// A row with only a lower bound, to which an upper bound can be added.
///
/// `<=` with a number on the left of a linear operand, and `>=` with a number on the right, return a
/// `HighsLowerBoundedLinearExpression`. `<=` with a number on its right adds the upper bound and returns a
/// `HighsBoundedLinearExpression`. A `HighsLowerBoundedLinearExpression` converts implicitly to a
/// `HighsBoundedLinearExpression` with no upper bound, so `addConstr` accepts it as it is.
class HighsLowerBoundedLinearExpression {
 public:
  /// Constructs the row `lower <= expression`.
  HighsLowerBoundedLinearExpression(HighsLinearExpression expression, double lower)
      : expression_{std::move(expression)}, lower_{lower} {}

  /// Returns the row as a `HighsBoundedLinearExpression` whose upper bound is `kHighsInf`.
  ///
  /// The conversion is implicit. For an rvalue, the conversion moves the expression into the result.
  // NOLINTNEXTLINE(*-explicit-constructor): a row with one bound is a row
  operator HighsBoundedLinearExpression() const& { return {expression_, lower_, kHighsInf}; }

  /// @overload
  // NOLINTNEXTLINE(*-explicit-constructor): a row with one bound is a row
  operator HighsBoundedLinearExpression() && { return {std::move(expression_), lower_, kHighsInf}; }

  /// Returns a row with the lower bound of `row` and the upper bound `upper`.
  friend HighsBoundedLinearExpression operator<=(HighsLowerBoundedLinearExpression row, double upper) {
    return {std::move(row.expression_), row.lower_, upper};
  }

 private:
  HighsLinearExpression expression_;
  double lower_;
};

/// A row with only an upper bound, to which a lower bound can be added.
///
/// `<=` with a number on the right of a linear operand, and `>=` with a number on the left, return a
/// `HighsUpperBoundedLinearExpression`. `>=` with a number on its right adds the lower bound and returns a
/// `HighsBoundedLinearExpression`. A `HighsUpperBoundedLinearExpression` converts implicitly to a
/// `HighsBoundedLinearExpression` with no lower bound, so `addConstr` accepts it as it is.
class HighsUpperBoundedLinearExpression {
 public:
  /// Constructs the row `expression <= upper`.
  HighsUpperBoundedLinearExpression(HighsLinearExpression expression, double upper)
      : expression_{std::move(expression)}, upper_{upper} {}

  /// Returns the row as a `HighsBoundedLinearExpression` whose lower bound is `-kHighsInf`.
  ///
  /// The conversion is implicit. For an rvalue, the conversion moves the expression into the result.
  // NOLINTNEXTLINE(*-explicit-constructor): a row with one bound is a row
  operator HighsBoundedLinearExpression() const& { return {expression_, -kHighsInf, upper_}; }

  /// @overload
  // NOLINTNEXTLINE(*-explicit-constructor): a row with one bound is a row
  operator HighsBoundedLinearExpression() && { return {std::move(expression_), -kHighsInf, upper_}; }

  /// Returns a row with the upper bound of `row` and the lower bound `lower`.
  friend HighsBoundedLinearExpression operator>=(HighsUpperBoundedLinearExpression row, double lower) {
    return {std::move(row.expression_), lower, row.upper_};
  }

 private:
  HighsLinearExpression expression_;
  double upper_;
};

// A number is a value of an arithmetic type other than bool, or a column that holds one, such as a period's number
// used as a coefficient.

/// @hidden - a trait that the constraints of the operators use
template <typename T>
inline constexpr bool is_highs_scalar = std::is_arithmetic_v<T> && !std::same_as<T, bool>;
/// @hidden - a trait that the constraints of the operators use
template <typename T>
  requires internal::column<T>
inline constexpr bool is_highs_scalar<T> =
    std::is_arithmetic_v<typename T::value_type> && !std::same_as<typename T::value_type, bool>;
/// @hidden - a trait that the constraints of the operators use
template <typename T>
inline constexpr bool is_highs_linear_term =
    std::same_as<T, HighsVar> || std::same_as<T, HighsLinearTerm> || std::same_as<T, HighsLinearExpression>;
/// @hidden - a trait that the constraints of the operators use
template <typename T>
inline constexpr bool is_highs_linear = is_highs_scalar<T> || is_highs_linear_term<T>;
/// @hidden - a trait that the constraints of the operators use
template <typename T>
inline constexpr bool is_highs_quadratic =
    std::same_as<T, HighsQuadraticTerm> || std::same_as<T, HighsQuadraticExpression>;
/// @hidden - a trait that the constraints of the operators use
template <typename T>
inline constexpr bool is_highs_row =
    std::same_as<T, HighsBoundedLinearExpression> || std::same_as<T, HighsLowerBoundedLinearExpression> ||
    std::same_as<T, HighsUpperBoundedLinearExpression>;

// Two operands of a linear operation. At least one of them must be a variable, a term or an expression.

/// @hidden - a trait that the constraints of the operators use
template <typename L, typename R>
concept linear_operands =
    is_highs_linear<std::remove_cvref_t<L>> && is_highs_linear<std::remove_cvref_t<R>> &&
    (is_highs_linear_term<std::remove_cvref_t<L>> || is_highs_linear_term<std::remove_cvref_t<R>>);

// Two operands of a quadratic sum. At least one of them must be quadratic, and the other may be quadratic or linear.

/// @hidden - a trait that the constraints of the operators use
template <typename L, typename R>
concept quadratic_operands =
    (is_highs_quadratic<std::remove_cvref_t<L>> || is_highs_quadratic<std::remove_cvref_t<R>>) &&
    (is_highs_quadratic<std::remove_cvref_t<L>> || is_highs_linear<std::remove_cvref_t<L>>) &&
    (is_highs_quadratic<std::remove_cvref_t<R>> || is_highs_linear<std::remove_cvref_t<R>>);
/// @hidden - a trait that the constraints of the operators use
template <typename T>
concept scalar = is_highs_scalar<std::remove_cvref_t<T>>;

}  // namespace dreamor::highs
