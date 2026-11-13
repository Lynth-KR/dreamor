// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The IBM ILOG Concert backend for CPLEX: dreamor::concert::VarTable and dreamor::concert::sumExpr.
#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/index_set.hpp"  // Included as a guarantee to the user, because a model needs its sets.
#include "dreamor/internal/backend/row_index.hpp"
#include "dreamor/internal/backend/sum_builder.hpp"
#include "dreamor/internal/backend/var_table_core.hpp"
#include "dreamor/internal/index_set/indexed.hpp"
#include "dreamor/internal/index_set/key_args.hpp"
#include "dreamor/param_table.hpp"
#include "dreamor/solution_table.hpp"

#include "dreamor/internal/base/third_party_headers.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#include <ilconcert/iloexpression.h>
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::concert::detail {

// A table's variables as one IloNumVarArray, which the first solution() gathers and later calls reuse. A loop that
// solves many times then reads the values without gathering the variables each time, and building the model does not
// pay for the array. If several threads call solution() at once, the array is gathered once. The array belongs to the
// environment, as every Concert array does: env.end() frees it, and the table does not end it. The struct is outside
// VarTable because none of it depends on the keys, so every table shares one copy of this code.
struct GatheredVars {
  std::once_flag once;
  std::unique_ptr<IloNumVarArray> array;
};

[[nodiscard]] inline const IloNumVarArray& gather(GatheredVars& gathered,
                                                  const IloEnv& env,
                                                  const std::vector<IloNumVar>& vars) {
  std::call_once(gathered.once, [&] {
    auto array = std::make_unique<IloNumVarArray>(env);
    for (const IloNumVar& var : vars) {
      array->add(var);
    }
    gathered.array = std::move(array);
  });
  return *gathered.array;
}

// A Concert expression that a number can multiply: a variable, a term or an expression.
template <typename Expr>
concept concert_expression = !internal::column<Expr> && std::is_convertible_v<const Expr&, IloExpr>;

}  // namespace dreamor::concert::detail

namespace dreamor::concert {

/// @section dreamor::concert
///
/// The parts of this section build a model with IBM ILOG Concert Technology, the modeling layer of CPLEX. They use
/// Concert's own types:
///
/// - A table makes its variables in an `IloEnv`.
/// - The type of a table's variables is `ILOFLOAT`, `ILOINT` or `ILOBOOL`.
/// - A sum builds into an `IloExpr`.
/// - `solution()` reads the values from an `IloCplex`.
///
/// The header includes Concert's expressions, `<ilconcert/iloexpression.h>`, and no solver. A program that builds an
/// `IloModel` and solves it with an `IloCplex` includes `<ilcplex/ilocplex.h>` itself. `solution()` takes the
/// algorithm as a template parameter, so the header does not need the declaration of `IloCplex`.
///
/// A table's variables are ordinary Concert objects that belong to their environment. A model changes their bounds
/// with Concert's own functions, as for any Concert variable.
///
/// Make one table for each family of variables, on the whole set of the family. The rows of a subset or of a group
/// reach their variables through that table. A second table on a subset would make a second variable for each of its
/// rows, unrelated to the variable of the first table.
///
/// Concert's objects are not freed when they go out of scope: a program frees each one with `end()`, or frees all of
/// them with the environment's `end()`. A table ends none of its variables, neither while the table exists nor when
/// the table is destroyed. The `IloExpr` that `build()` returns belongs to the caller, who ends the `IloExpr` after
/// the constraint or the objective made from it is added to the model.
///
/// Guide: guide/backends.md

/// @hidden - the type of semiContinuous
struct SemiContinuous {};

/// A tag that selects the constructor of `VarTable` that makes semi-continuous variables.
inline constexpr SemiContinuous semiContinuous{};  // NOLINT(readability-identifier-naming): a tag, as std::in_place

/// A family of Concert variables, one for each row of a set.
///
/// The members that look up and iterate the variables are those of `VarTableCore`, in the VarTable section. The
/// table does not end its variables: the variables remain in their environment.
///
/// Takes: `Keys` must be one or more distinct columns. Each column of `Keys` must hold a value that can be written in
/// a name, as the Names section describes.
template <typename... Keys>
  requires(sizeof...(Keys) > 0 && (internal::nameable<Keys> && ...))
class VarTable : public internal::VarTableCore<IloNumVar, Keys...> {
  using Core = internal::VarTableCore<IloNumVar, Keys...>;

 public:
  using key_type = Core::key_type;
  using Core::Core;

  /// @hidden - exists only to reject, with a message, a name given in the place of the variables' type
  template <typename Lb, typename Ub, typename Name>
    requires std::is_convertible_v<const Name&, std::string_view>
  VarTable(
      const IloEnv& /*env*/, const IndexSet<Keys...>& set, const Lb& /*lb*/, const Ub& /*ub*/, const Name& /*name*/)
      : Core{set} {
    static_assert(internal::always_false<Name>,
                  "A VarTable takes the variables' type before their name: VarTable{env, set, lb, ub, ILOFLOAT, "
                  "\"Make\"}.");
  }

  /// @hidden - exists only to reject, with a message, nullptr given as a name
  template <typename Lb, typename Ub>
  VarTable(const IloEnv& /*env*/,
           const IndexSet<Keys...>& set,
           const Lb& /*lb*/,
           const Ub& /*ub*/,
           IloNumVar::Type /*type*/,
           std::nullptr_t /*name*/)
      : Core{set} {
    static_assert(internal::always_false<Lb>, DREAMOR_INTERNAL_NULL_NAME_MESSAGE);
  }

  /// @hidden - exists only to reject, with a message, nullptr given as a name
  template <typename Lb, typename Ub>
  VarTable(SemiContinuous /*unused*/,
           const IloEnv& /*env*/,
           const IndexSet<Keys...>& set,
           const Lb& /*sclb*/,
           const Ub& /*ub*/,
           IloNumVar::Type /*type*/,
           std::nullptr_t /*name*/)
      : Core{set} {
    static_assert(internal::always_false<Lb>, DREAMOR_INTERNAL_NULL_NAME_MESSAGE);
  }

  /// Constructs a table with one variable in `env` for each row of `set`.
  ///
  /// The variables are made in the order of the rows of `set`. Each variable is named `<name>_<key0>_<key1>...`, as
  /// the VarTable section describes.
  ///
  /// `lb` and `ub` are the lower bound and the upper bound of the variables. Each bound is one of these:
  ///
  /// - A number, which is the bound of every variable.
  /// - A column callable that reads columns of `set` and returns a number. The callable is called once for each row,
  ///   and the number is the bound of the row's variable. A `ParamTable` of numbers is such a callable.
  ///
  /// `type` is the type of the variables: `ILOFLOAT`, `ILOINT` or `ILOBOOL`.
  ///
  /// Takes: `lb` and `ub` must each be a number, or a column callable that reads columns of `set` and returns a value
  /// that converts to `double`. If `lb` is left out, the lower bound is 0. If `ub` is left out, the upper bound is
  /// `IloInfinity`. If `type` is left out, the variables are continuous. `name` may be left out or empty, and the
  /// variables then have no names.
  template <typename Lb = IloNum, typename Ub = IloNum>
  VarTable(const IloEnv& env,
           const IndexSet<Keys...>& set,
           const Lb& lb          = 0,
           const Ub& ub          = IloInfinity,
           IloNumVar::Type type  = ILOFLOAT,
           std::string_view name = {})
      : Core{set} {
    internal::checkBound<Lb, key_type>();
    internal::checkBound<Ub, key_type>();
    typename Core::NameBuilder names = Core::namesFor(name);
    this->vars_.reserve(this->rows().size());
    for (const key_type& row : this->rows()) {
      this->vars_.emplace_back(
          env, internal::boundOf(lb, row), internal::boundOf(ub, row), type, Core::nameOf(names, row));
    }
  }

  /// @overload
  /// Constructs a table with one semi-continuous variable in `env` for each row of `set`.
  ///
  /// The variables are of Concert's type `IloSemiContVar`. The value `x` of each variable is either 0 or in its range:
  /// `x = 0` or `sclb <= x <= ub`. The table holds each variable as an `IloNumVar`, so a lookup returns an `IloNumVar`.
  ///
  /// The variables are made in the order of the rows of `set`. Each variable is named `<name>_<key0>_<key1>...`, as
  /// the VarTable section describes.
  ///
  /// `sclb` and `ub` are the lower bound and the upper bound of the range. Each bound is one of these:
  ///
  /// - A number, which is the bound of every variable.
  /// - A column callable that reads columns of `set` and returns a number. The callable is called once for each row,
  ///   and the number is the bound of the row's variable. A `ParamTable` of numbers is such a callable.
  ///
  /// `type` is the type of the variables, such as `ILOFLOAT` or `ILOINT`.
  ///
  /// Takes: `semiContinuous` must be the first argument. `sclb` and `ub` must each be a number, or a column callable
  /// that reads columns of `set` and returns a value that converts to `double`. If `type` is left out, the variables
  /// are continuous. `name` may be left out or empty, and the variables then have no names.
  template <typename Lb = IloNum, typename Ub = IloNum>
  VarTable(SemiContinuous /*unused*/,
           const IloEnv& env,
           const IndexSet<Keys...>& set,
           const Lb& sclb,
           const Ub& ub,
           IloNumVar::Type type  = ILOFLOAT,
           std::string_view name = {})
      : Core{set} {
    internal::checkBound<Lb, key_type>();
    internal::checkBound<Ub, key_type>();
    typename Core::NameBuilder names = Core::namesFor(name);
    this->vars_.reserve(this->rows().size());
    for (const key_type& row : this->rows()) {
      this->vars_.push_back(IloSemiContVar(
          env, internal::boundOf(sclb, row), internal::boundOf(ub, row), type, Core::nameOf(names, row)));
    }
  }

  /// Returns the value of each variable in the solution that `algorithm` holds, in a `SolutionTable` on the table's
  /// set.
  ///
  /// `solution()` reads every value with one call to `algorithm.getValues`. The first call to `solution()` gathers the
  /// variables into an `IloNumVarArray` in the environment, and later calls use the same array. If several threads
  /// call `solution()` at once, the array is gathered once.
  ///
  /// A variable that no constraint and no objective uses is not extracted by Concert, and `getValues` throws an
  /// `IloException` for an array that holds such a variable. If `getValues` throws an `IloException` and `algorithm`
  /// has `isExtracted` and `getValue`, the values are read one variable at a time:
  ///
  /// - A variable that `algorithm` extracted has the value that `getValue` returns.
  /// - A variable that `algorithm` did not extract has the value 0.
  ///
  /// In a debug build, a warning in the log then gives the number of variables that were not extracted, and the names
  /// of the first five. Such a variable can be a sign of a constraint left out by mistake. If `algorithm` has no
  /// `isExtracted` or no `getValue`, the exception from `getValues` propagates. An exception from `getValue` also
  /// propagates.
  ///
  /// Takes: `algorithm` must have `getEnv()` and `getValues(IloNumArray, IloNumVarArray)`, as an `IloCplex` has.
  /// `algorithm` may also have `isExtracted` and `getValue`, which `solution()` then uses as described above.
  template <typename Algorithm>
  [[nodiscard]] SolutionTable<Keys...> solution(const Algorithm& algorithm) const {
    std::vector<double> values(this->vars_.size());
    if (!values.empty()) {
      const IloEnv env = algorithm.getEnv();
      IloNumArray out(env);
      try {
        algorithm.getValues(out, detail::gather(*gathered_, env, this->vars_));
        for (std::size_t i = 0; i < values.size(); ++i) {
          values[i] = out[static_cast<IloInt>(i)];
        }
      } catch (const IloException&) {
        out.end();
        if constexpr (requires(const IloNumVar& var) {
                        { algorithm.isExtracted(var) } -> std::convertible_to<bool>;
                        { algorithm.getValue(var) } -> std::convertible_to<double>;
                      }) {
          valuesOneByOne(algorithm, values);
        } else {
          throw;
        }
        return this->solutionTable(std::move(values));
      }
      out.end();  // out is the only Concert object that solution() makes, so it is ended here.
    }
    return this->solutionTable(std::move(values));
  }

 private:
  // Reads the values for solution() if some variable was not extracted: the value of each extracted variable, and 0
  // for each other variable.
  template <typename Algorithm>
  void valuesOneByOne(const Algorithm& algorithm, std::vector<double>& values) const {
    [[maybe_unused]] std::size_t unused = 0;
    [[maybe_unused]] std::string names;
    for (std::size_t i = 0; i < values.size(); ++i) {
      const IloNumVar& var = this->vars_[i];
      if (algorithm.isExtracted(var)) {
        values[i] = algorithm.getValue(var);
        continue;
      }
      values[i] = 0.0;
#ifndef NDEBUG
      if (++unused <= kUnusedNamed) { names += std::string{names.empty() ? "" : ", "} + variableName(var); }
#endif
    }
#ifndef NDEBUG
    DREAMOR_LOG_WARNING(
        "VarTable::solution: {} of the family's variables appear in no row or objective, so Concert did not "
        "extract them; read as 0: {}{}",
        unused,
        names,
        unused > kUnusedNamed ? ", ..." : "");
#endif
  }

  static constexpr std::size_t kUnusedNamed = 5;  // How many variables a debug build's warning names.

  [[nodiscard]] static std::string variableName(const IloNumVar& var) {
    const char* const name = var.getName();
    return name != nullptr ? std::string{name} : std::string{"(unnamed)"};
  }

  std::unique_ptr<detail::GatheredVars> gathered_ = std::make_unique<detail::GatheredVars>();
};

/// Returns the sum of `term` over `rows`, which is not built yet.
///
/// `build()` makes the sum into an `IloExpr` in `env`: the sum of `term(r)` over the rows `r`. The caller ends the
/// `IloExpr` after using it. The Sums section describes the rows, the term, `where` and `whereEach`.
///
/// Takes: If `rows` is a set or the rows of a group, `Columns` must be empty. If `rows` is a container, `Columns` must
/// be the one column that the container's elements fill.
/// Contract: column_integer_in_range
template <typename... Columns, typename Rows, typename Term>
[[nodiscard]] auto sumExpr(IloEnv env, Rows&& rows, Term&& term) {
  return internal::makeSum<Columns...>(
      [env] { return IloExpr{env}; }, std::forward<Rows>(rows), std::forward<Term>(term));
}

/// @hidden - exists only to reject, with a message, a call without the IloEnv
template <typename... Columns, typename Rows, typename Term>
auto sumExpr(Rows&& /*rows*/, Term&& /*term*/) {
  static_assert(internal::always_false<Rows>,
                "Concert's sumExpr takes the IloEnv first: sumExpr(env, rows, term). The one without an "
                "environment is MathOpt's, dreamor::mathopt::sumExpr(rows, term).");
}

}  // namespace dreamor::concert

namespace dreamor {

/// Returns the product of the value of `coefficient`, converted to `IloNum`, and `expression`.
///
/// With this operator, a column of numbers multiplies a Concert expression as it multiplies an expression of the
/// MathOpt or the HiGHS backend. Concert's own operators take an `IloNum`, an `IloInt` or an `int`. A column of
/// another type of number, such as `std::uint32_t`, matches none of them exactly, so without this operator the call
/// would be ambiguous.
///
/// Takes: `C` must be a column whose value type is a number. `Expr` must be a Concert expression that converts to
/// `IloExpr`, such as a variable, a term or an `IloExpr`.
template <typename C, typename Expr>
  requires internal::column<C> && std::is_arithmetic_v<typename C::value_type> &&
           concert::detail::concert_expression<Expr>
[[nodiscard]] auto operator*(const C& coefficient, const Expr& expression) {
  // Argument-dependent lookup finds these operators through the column's base class, Col, which is in namespace
  // dreamor.
  return static_cast<IloNum>(coefficient.get()) * expression;
}

/// @overload
/// Returns the product of `expression` and the value of `coefficient`, converted to `IloNum`.
///
/// This form takes the column second. With this operator, a column of numbers multiplies a Concert expression as it
/// multiplies an expression of the MathOpt or the HiGHS backend. Concert's own operators take an `IloNum`, an `IloInt`
/// or an `int`. A column of another type of number, such as `std::uint32_t`, matches none of them exactly, so without
/// this operator the call would be ambiguous.
///
/// Takes: `C` must be a column whose value type is a number. `Expr` must be a Concert expression that converts to
/// `IloExpr`, such as a variable, a term or an `IloExpr`.
template <typename Expr, typename C>
  requires internal::column<C> && std::is_arithmetic_v<typename C::value_type> &&
           concert::detail::concert_expression<Expr>
[[nodiscard]] auto operator*(const Expr& expression, const C& coefficient) {
  return expression * static_cast<IloNum>(coefficient.get());
}

}  // namespace dreamor
