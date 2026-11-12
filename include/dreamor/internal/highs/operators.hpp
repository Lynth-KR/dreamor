// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The HiGHS backend's operators: what makes expressions and rows of variables, terms and numbers.
#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "dreamor/internal/highs/expression.hpp"
#include "dreamor/internal/index_set/column.hpp"

#include "dreamor/internal/base/third_party_headers.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#include "Highs.h"
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::highs::detail {

// Returns the HighsEnv that the variables of an expression belong to, or null if the expression has no variables.
// The debug checks and the expansion of a product use it. It is not a public member, so that no raw pointer leaves
// the public API.
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

// Converts an operand to an expression. A number is converted with a cast, because a braced initialization would
// reject the narrowing conversion from an integer to double. An expression that is an rvalue is moved.
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

}  // namespace dreamor::highs::detail

namespace dreamor::highs {

/// @section dreamor::highs

/// @anchor operator-negate
/// Returns the negation of a variable, a term or an expression.
///
/// The result depends on the operand:
///
/// - A `HighsVar` becomes a `HighsLinearTerm` with the coefficient -1.
/// - A `HighsLinearTerm` or a `HighsQuadraticTerm` becomes a term of the same type with the opposite coefficient.
/// - A `HighsLinearExpression` or a `HighsQuadraticExpression` becomes an expression of the same type in which every
///   coefficient and the constant are negated.
inline HighsLinearTerm operator-(HighsVar variable) noexcept {
  return {.variable = variable, .coefficient = -1.0};
}

/// @overload
inline HighsLinearTerm operator-(const HighsLinearTerm& term) noexcept {
  return {.variable = term.variable, .coefficient = -term.coefficient};
}

/// @overload
inline HighsLinearExpression operator-(HighsLinearExpression expression) {
  expression *= -1.0;
  return expression;
}

/// @overload
inline HighsQuadraticTerm operator-(const HighsQuadraticTerm& term) noexcept {
  return {.first = term.first, .second = term.second, .coefficient = -term.coefficient};
}

/// @overload
inline HighsQuadraticExpression operator-(HighsQuadraticExpression expression) {
  expression *= -1.0;
  return expression;
}

/// @anchor operator-star-number
/// Returns a variable, a term or an expression multiplied by a number.
///
/// The number may be on either side. The result depends on the other operand:
///
/// - A `HighsVar` becomes a `HighsLinearTerm` whose coefficient is the number.
/// - A `HighsLinearTerm` or a `HighsQuadraticTerm` becomes a term of the same type whose coefficient is multiplied by
///   the number.
/// - A `HighsLinearExpression` or a `HighsQuadraticExpression` becomes an expression of the same type in which every
///   coefficient and the constant are multiplied by the number.
///
/// Takes: `S` must be a number: an arithmetic type other than `bool`, or a column that holds such a type.
template <scalar S>
HighsLinearTerm operator*(S scale, HighsVar variable) noexcept {
  return {.variable = variable, .coefficient = static_cast<double>(scale)};
}

/// @overload
template <scalar S>
HighsLinearTerm operator*(HighsVar variable, S scale) noexcept {
  return {.variable = variable, .coefficient = static_cast<double>(scale)};
}

/// @overload
template <scalar S>
HighsLinearTerm operator*(S scale, const HighsLinearTerm& term) noexcept {
  return {.variable = term.variable, .coefficient = static_cast<double>(scale) * term.coefficient};
}

/// @overload
template <scalar S>
HighsLinearTerm operator*(const HighsLinearTerm& term, S scale) noexcept {
  return {.variable = term.variable, .coefficient = static_cast<double>(scale) * term.coefficient};
}

/// @overload
template <scalar S>
HighsLinearExpression operator*(S scale, HighsLinearExpression expression) {
  expression *= static_cast<double>(scale);
  return expression;
}

/// @overload
template <scalar S>
HighsLinearExpression operator*(HighsLinearExpression expression, S scale) {
  expression *= static_cast<double>(scale);
  return expression;
}

/// @overload
template <scalar S>
HighsQuadraticTerm operator*(S scale, const HighsQuadraticTerm& term) noexcept {
  return {.first = term.first, .second = term.second, .coefficient = static_cast<double>(scale) * term.coefficient};
}

/// @overload
template <scalar S>
HighsQuadraticTerm operator*(const HighsQuadraticTerm& term, S scale) noexcept {
  return {.first = term.first, .second = term.second, .coefficient = static_cast<double>(scale) * term.coefficient};
}

/// @overload
template <scalar S>
HighsQuadraticExpression operator*(S scale, HighsQuadraticExpression expression) {
  expression *= static_cast<double>(scale);
  return expression;
}

/// @overload
template <scalar S>
HighsQuadraticExpression operator*(HighsQuadraticExpression expression, S scale) {
  expression *= static_cast<double>(scale);
  return expression;
}

/// Returns a variable, a term or an expression divided by a number.
///
/// `/` takes the number only on its right. A `HighsVar` divided by a number becomes a `HighsLinearTerm` whose
/// coefficient is `1 / divisor`. A term becomes a term of the same type whose coefficient is divided by `divisor`. In
/// an expression, every coefficient and the constant are multiplied by `1 / divisor`. dreamor does not check whether
/// `divisor` is 0.
///
/// Takes: `S` must be a number: an arithmetic type other than `bool`, or a column that holds such a type.
template <scalar S>
HighsLinearTerm operator/(HighsVar variable, S divisor) noexcept {
  return {.variable = variable, .coefficient = 1.0 / static_cast<double>(divisor)};
}

/// @overload
template <scalar S>
HighsLinearTerm operator/(const HighsLinearTerm& term, S divisor) noexcept {
  return {.variable = term.variable, .coefficient = term.coefficient / static_cast<double>(divisor)};
}

/// @overload
template <scalar S>
HighsLinearExpression operator/(HighsLinearExpression expression, S divisor) {
  expression /= static_cast<double>(divisor);
  return expression;
}

/// @overload
template <scalar S>
HighsQuadraticTerm operator/(const HighsQuadraticTerm& term, S divisor) noexcept {
  return {.first = term.first, .second = term.second, .coefficient = term.coefficient / static_cast<double>(divisor)};
}

/// @overload
template <scalar S>
HighsQuadraticExpression operator/(HighsQuadraticExpression expression, S divisor) {
  expression /= static_cast<double>(divisor);
  return expression;
}

/// @anchor operator-star-terms
/// Returns the product of two operands that are each a `HighsVar` or a `HighsLinearTerm`.
///
/// The product is a `HighsQuadraticTerm`. Its coefficient is the product of the two coefficients, and a `HighsVar`
/// has the coefficient 1. To multiply by a number, use the `operator*` that takes a number.
inline HighsQuadraticTerm operator*(HighsVar first, HighsVar second) noexcept {
  return {.first = first, .second = second, .coefficient = 1.0};
}

/// @overload
inline HighsQuadraticTerm operator*(const HighsLinearTerm& first, HighsVar second) noexcept {
  return {.first = first.variable, .second = second, .coefficient = first.coefficient};
}

/// @overload
inline HighsQuadraticTerm operator*(HighsVar first, const HighsLinearTerm& second) noexcept {
  return {.first = first, .second = second.variable, .coefficient = second.coefficient};
}

/// @overload
inline HighsQuadraticTerm operator*(const HighsLinearTerm& first, const HighsLinearTerm& second) noexcept {
  return {.first = first.variable, .second = second.variable, .coefficient = first.coefficient * second.coefficient};
}

/// @overload
/// Returns the expanded product of two linear operands, at least one of which is a `HighsLinearExpression`.
///
/// The product is a `HighsQuadraticExpression`. Each term of one operand is multiplied by each term of the other.
/// The constant of each operand is multiplied by the terms of the other operand, and the two constants are
/// multiplied together. For operands `a + l` and `c + r`, with constants `a` and `c`, the product is
/// `ac + c l + a r + l r`.
///
/// Takes: `lhs` and `rhs` must each be a `HighsVar`, a `HighsLinearTerm` or a `HighsLinearExpression`. At least one
/// of them must be a `HighsLinearExpression`.
/// Contract: highs_one_environment
template <typename L, typename R>
  requires(is_highs_linear_term<std::remove_cvref_t<L>> && is_highs_linear_term<std::remove_cvref_t<R>> &&
           (std::same_as<std::remove_cvref_t<L>, HighsLinearExpression> ||
            std::same_as<std::remove_cvref_t<R>, HighsLinearExpression>))
HighsQuadraticExpression operator*(const L& lhs, const R& rhs) {
  const HighsLinearExpression left  = detail::toHighsLinear(lhs);
  const HighsLinearExpression right = detail::toHighsLinear(rhs);
  const HighsEnv* left_model        = detail::HighsModelAccess::of(left);
  const HighsEnv* right_model       = detail::HighsModelAccess::of(right);
#ifndef NDEBUG
  // The product rebuilds every variable with one environment, so a mix of two must be stopped here, as a sum's
  // append stops it.
  if (left_model != nullptr && right_model != nullptr && left_model != right_model) {
    DREAMOR_DEBUG_FATAL(
        "dreamor::highs: one expression holds variables of two HighsEnvs - a broken contract (dreamor/highs.hpp).");
  }
#endif
  const HighsEnv* model = left_model != nullptr ? left_model : right_model;
  // (a + l)(c + r) = ac + c l + a r + l r. A side whose constant is zero adds no linear terms.
  const double a = left.constant();
  const double c = right.constant();
  HighsQuadraticExpression product(a * c);
  // An expression records its model when it gets its first term. If neither side has a term, the product is ac.
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

/// Returns the sum of two linear operands.
///
/// The sum is a `HighsLinearExpression`. If either operand is quadratic, the form that returns a
/// `HighsQuadraticExpression` applies instead.
///
/// Takes: `lhs` and `rhs` must each be a linear operand. At least one of them must not be a number.
/// Contract: highs_one_environment
/// Cost: If `lhs` is an rvalue expression, the sum reuses the storage of `lhs` and appends the terms of `rhs` to it.
/// A sum of many operands therefore does not copy the expression at each `+`.
template <typename L, typename R>
  requires linear_operands<L, R>
HighsLinearExpression operator+(L&& lhs, R&& rhs) {
  HighsLinearExpression sum = detail::toHighsLinear(std::forward<L>(lhs));
  if constexpr (is_highs_scalar<std::remove_cvref_t<R>>) {
    sum += static_cast<double>(rhs);
  } else {
    sum += std::forward<R>(rhs);
  }
  return sum;
}

/// @anchor operator-minus
/// Returns the difference of two linear operands.
///
/// The difference is a `HighsLinearExpression`. If either operand is quadratic, the form that returns a
/// `HighsQuadraticExpression` applies instead.
///
/// Takes: `lhs` and `rhs` must each be a linear operand. At least one of them must not be a number.
/// Contract: highs_one_environment
/// Cost: If `lhs` is an rvalue expression, the difference reuses the storage of `lhs` and appends the terms of `rhs`
/// to it, with their signs turned. A long difference therefore does not copy the expression at each `-`.
template <typename L, typename R>
  requires linear_operands<L, R>
HighsLinearExpression operator-(L&& lhs, R&& rhs) {
  HighsLinearExpression difference = detail::toHighsLinear(std::forward<L>(lhs));
  if constexpr (is_highs_scalar<std::remove_cvref_t<R>>) {
    difference -= static_cast<double>(rhs);
  } else {
    difference -= std::forward<R>(rhs);
  }
  return difference;
}

/// @overload
/// Returns the sum of two operands, at least one of which is quadratic.
///
/// The sum is a `HighsQuadraticExpression`. A quadratic operand is a `HighsQuadraticTerm` or a
/// `HighsQuadraticExpression`.
///
/// Takes: At least one of `lhs` and `rhs` must be quadratic. The other may be quadratic or a linear operand.
/// Contract: highs_one_environment
/// Cost: If `lhs` is an rvalue expression, the sum reuses the storage of `lhs`.
template <typename L, typename R>
  requires quadratic_operands<L, R>
HighsQuadraticExpression operator+(L&& lhs, R&& rhs) {
  HighsQuadraticExpression sum = detail::toHighsQuadratic(std::forward<L>(lhs));
  sum += detail::toHighsQuadratic(std::forward<R>(rhs));
  return sum;
}

/// @overload
/// Returns the difference of two operands, at least one of which is quadratic.
///
/// The difference is a `HighsQuadraticExpression`. A quadratic operand is a `HighsQuadraticTerm` or a
/// `HighsQuadraticExpression`.
///
/// Takes: At least one of `lhs` and `rhs` must be quadratic. The other may be quadratic or a linear operand.
/// Contract: highs_one_environment
/// Cost: If `lhs` is an rvalue expression, the difference reuses the storage of `lhs`.
template <typename L, typename R>
  requires quadratic_operands<L, R>
HighsQuadraticExpression operator-(L&& lhs, R&& rhs) {
  HighsQuadraticExpression difference = detail::toHighsQuadratic(std::forward<L>(lhs));
  difference -= detail::toHighsQuadratic(std::forward<R>(rhs));
  return difference;
}

/// Returns a row that bounds `lhs` from above by `rhs`.
///
/// The result depends on which operand is a number:
///
/// - If `rhs` is a number, the result is a `HighsUpperBoundedLinearExpression` with the upper bound `rhs`. `>=`
///   with a number on its right adds a lower bound.
/// - If `lhs` is a number, the result is a `HighsLowerBoundedLinearExpression` with the lower bound `lhs`. `<=` with
///   a number on its right adds an upper bound.
/// - Otherwise, the result is the `HighsBoundedLinearExpression` `lhs - rhs <= 0`.
///
/// A comparison that has a quadratic operand does not compile, because HiGHS accepts quadratic terms only in the
/// objective.
///
/// Takes: `lhs` and `rhs` must each be a linear operand, and at least one of them must not be a number.
template <typename L, typename R>
  requires linear_operands<L, R>
auto operator<=(L&& lhs, R&& rhs) {
  if constexpr (is_highs_scalar<std::remove_cvref_t<R>>) {
    return HighsUpperBoundedLinearExpression{detail::toHighsLinear(std::forward<L>(lhs)), static_cast<double>(rhs)};
  } else if constexpr (is_highs_scalar<std::remove_cvref_t<L>>) {
    return HighsLowerBoundedLinearExpression{detail::toHighsLinear(std::forward<R>(rhs)), static_cast<double>(lhs)};
  } else {
    return HighsBoundedLinearExpression{std::forward<L>(lhs) - std::forward<R>(rhs), -kHighsInf, 0.0};
  }
}

/// Returns a row that bounds `lhs` from below by `rhs`.
///
/// The result depends on which operand is a number:
///
/// - If `rhs` is a number, the result is a `HighsLowerBoundedLinearExpression` with the lower bound `rhs`. `<=`
///   with a number on its right adds an upper bound.
/// - If `lhs` is a number, the result is a `HighsUpperBoundedLinearExpression` with the upper bound `lhs`. `>=` with
///   a number on its right adds a lower bound.
/// - Otherwise, the result is the `HighsBoundedLinearExpression` `lhs - rhs >= 0`.
///
/// A comparison that has a quadratic operand does not compile, because HiGHS accepts quadratic terms only in the
/// objective.
///
/// Takes: `lhs` and `rhs` must each be a linear operand, and at least one of them must not be a number.
template <typename L, typename R>
  requires linear_operands<L, R>
auto operator>=(L&& lhs, R&& rhs) {
  if constexpr (is_highs_scalar<std::remove_cvref_t<R>>) {
    return HighsLowerBoundedLinearExpression{detail::toHighsLinear(std::forward<L>(lhs)), static_cast<double>(rhs)};
  } else if constexpr (is_highs_scalar<std::remove_cvref_t<L>>) {
    return HighsUpperBoundedLinearExpression{detail::toHighsLinear(std::forward<R>(rhs)), static_cast<double>(lhs)};
  } else {
    return HighsBoundedLinearExpression{std::forward<L>(lhs) - std::forward<R>(rhs), 0.0, kHighsInf};
  }
}

/// Returns a row that fixes `lhs - rhs` at 0.
///
/// The result is a `HighsBoundedLinearExpression` whose lower and upper bounds are equal. A comparison that has a
/// quadratic operand does not compile, because HiGHS accepts quadratic terms only in the objective.
///
/// Takes: `lhs` and `rhs` must each be a linear operand, and at least one of them must not be a number.
template <typename L, typename R>
  requires linear_operands<L, R>
HighsBoundedLinearExpression operator==(L&& lhs, R&& rhs) {
  return HighsBoundedLinearExpression{std::forward<L>(lhs) - std::forward<R>(rhs), 0.0, 0.0};
}

// These templates match the operands exactly. If one of them is chosen, its static_assert rejects the call with one
// message.

/// @hidden - exists only to reject a quadratic row with a message: HiGHS accepts quadratic terms only in objectives
template <typename L, typename R>
  requires quadratic_operands<L, R>
auto operator<=(const L& /*lhs*/, const R& /*rhs*/) {
  static_assert(internal::always_false<L>, DREAMOR_INTERNAL_QUADRATIC_ROW_MESSAGE);
  return false;
}

/// @hidden - exists only to reject a quadratic row with a message: HiGHS accepts quadratic terms only in objectives
template <typename L, typename R>
  requires quadratic_operands<L, R>
auto operator>=(const L& /*lhs*/, const R& /*rhs*/) {
  static_assert(internal::always_false<L>, DREAMOR_INTERNAL_QUADRATIC_ROW_MESSAGE);
  return false;
}

/// @hidden - exists only to reject a quadratic row with a message: HiGHS accepts quadratic terms only in objectives
template <typename L, typename R>
  requires quadratic_operands<L, R>
auto operator==(const L& /*lhs*/, const R& /*rhs*/) {
  static_assert(internal::always_false<L>, DREAMOR_INTERNAL_QUADRATIC_ROW_MESSAGE);
  return false;
}

}  // namespace dreamor::highs
