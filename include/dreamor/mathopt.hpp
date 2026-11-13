// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The OR-Tools MathOpt backend: dreamor::mathopt::VarTable and dreamor::mathopt::sumExpr.
#pragma once

#include <concepts>
#include <cstddef>
#include <limits>
#include <ranges>
#include <stdexcept>
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
#include "ortools/math_opt/cpp/model.h"
#include "ortools/math_opt/cpp/solve_result.h"
#include "ortools/math_opt/cpp/variable_and_expressions.h"
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::mathopt {

/// @section dreamor::mathopt
///
/// The parts of this section build a model with MathOpt, the modeling layer of OR-Tools. They use MathOpt's own
/// types:
///
/// - A table makes its variables in a `math_opt::Model`.
/// - A sum builds into a `math_opt::LinearExpression` or a `math_opt::QuadraticExpression`, which the model's
///   constraints and objective take.
/// - `solution()` reads the values from a `math_opt::SolveResult`.
///
/// MathOpt is one modeling API for several solvers, such as HiGHS, SCIP, GLOP, PDLP, CP-SAT, GLPK, Gurobi and Xpress,
/// so this backend reaches all of them. A MathOpt model has no semi-continuous variables.
///
/// The header includes MathOpt's model, expressions and solve result, and not `math_opt::Solve`. A program that
/// solves includes `ortools/math_opt/cpp/solve.h`, or `ortools/math_opt/cpp/math_opt.h`, itself.
///
/// To build against the prebuilt package of OR-Tools, a program needs these settings:
///
/// - The macro `OR_PROTO_DLL` is defined as empty.
/// - The package's `include/` directory is on the include path.
/// - The program links libortools, libprotobuf and the Abseil libraries.
///
/// MathOpt's objects are ordinary C++ values, and a program does not end them. A table's variables belong to the
/// `math_opt::Model`, and each variable refers to the model. A variable must be used only while its model exists.
///
/// Guide: guide/backends.md

/// A family of MathOpt variables, one for each row of a set.
///
/// The members that look up and iterate the variables are those of `VarTableCore`, in the VarTable section. The
/// variables belong to the model, not to the table.
///
/// Takes: `Keys` must be one or more distinct columns. Each column of `Keys` must hold a value that can be written in
/// a name, as the Names section describes.
template <typename... Keys>
  requires(sizeof...(Keys) > 0 && (internal::nameable<Keys> && ...))
class VarTable : public internal::VarTableCore<::operations_research::math_opt::Variable, Keys...> {
  using Core = internal::VarTableCore<::operations_research::math_opt::Variable, Keys...>;

 public:
  using key_type = Core::key_type;
  /// The type of the table's variables, which is MathOpt's `math_opt::Variable`.
  using Variable = ::operations_research::math_opt::Variable;
  using Core::Core;

  /// @anchor vartable-constructor-empty
  /// Constructs a table on the empty set, with no variables. A family is moved into it once, by assignment (the
  /// VarTable section).
  ///
  /// Cost: No memory is allocated.
  VarTable() noexcept = default;

  /// @hidden - exists only to reject, with a message, a name given in the place of is_integer
  template <typename Lb, typename Ub, typename Name>
    requires std::is_convertible_v<const Name&, std::string_view>
  VarTable(::operations_research::math_opt::Model& /*model*/,
           const IndexSet<Keys...>& set,
           const Lb& /*lb*/,
           const Ub& /*ub*/,
           const Name& /*name*/)
      : Core{set} {
    // Without this constructor, a name in the place of is_integer would convert to true, and the call would make an
    // unnamed family of integer variables.
    static_assert(internal::always_false<Name>,
                  "A VarTable takes is_integer before the variables' name: VarTable{model, set, lb, ub, false, "
                  "\"Make\"}.");
  }

  /// @hidden - exists only to reject, with a message, nullptr given as a name
  template <typename Lb, typename Ub>
  VarTable(::operations_research::math_opt::Model& /*model*/,
           const IndexSet<Keys...>& set,
           const Lb& /*lb*/,
           const Ub& /*ub*/,
           bool /*is_integer*/,
           std::nullptr_t /*name*/)
      : Core{set} {
    static_assert(internal::always_false<Lb>, DREAMOR_INTERNAL_NULL_NAME_MESSAGE);
  }

  /// @anchor vartable-constructor
  /// Constructs a table with one variable in `model` for each row of `set`.
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
  /// `is_integer` is MathOpt's own flag for the kind of the variables. If `is_integer` is true, the variables are
  /// integer variables; otherwise, they are continuous variables.
  ///
  /// Takes: `lb` and `ub` must each be a number, or a column callable that reads columns of `set` and returns a value
  /// that converts to `double`. If `lb` is left out, the lower bound is 0. If `ub` is left out, the upper bound is
  /// infinity. If `is_integer` is left out, the variables are continuous. `name` may be left out or empty, and the
  /// variables then have no names.
  template <typename Lb = double, typename Ub = double>
  VarTable(::operations_research::math_opt::Model& model,
           const IndexSet<Keys...>& set,
           const Lb& lb          = 0,
           const Ub& ub          = std::numeric_limits<double>::infinity(),
           bool is_integer       = false,
           std::string_view name = {})
      : Core{set} {
    internal::checkBound<Lb, key_type>();
    internal::checkBound<Ub, key_type>();
    typename Core::NameBuilder names = Core::namesFor(name);
    this->vars_.reserve(this->rows().size());
    for (const key_type& row : this->rows()) {
      const char* const variable_name = Core::nameOf(names, row);
      this->vars_.push_back(model.AddVariable(internal::boundOf(lb, row),
                                              internal::boundOf(ub, row),
                                              is_integer,
                                              variable_name != nullptr ? variable_name : ""));
    }
  }

  /// Returns the value of each variable in the best primal feasible solution of `result`, in a `SolutionTable` on the
  /// table's set.
  ///
  /// If `result` has no primal feasible solution, MathOpt itself would end the program with a failed `CHECK` when the
  /// values are read, so `solution()` throws an exception instead. If `result` comes from solving another model,
  /// `result` holds no value for the table's variables, and MathOpt's map of values throws when a value is looked up.
  ///
  /// Takes: `result` must come from solving the model that holds the table's variables.
  /// Throws: `std::logic_error` if `result` has no primal feasible solution; `std::out_of_range` if `result` comes from
  /// solving another model.
  [[nodiscard]] SolutionTable<Keys...> solution(const ::operations_research::math_opt::SolveResult& result) const {
    if (!result.has_primal_feasible_solution()) {
      throw std::logic_error("VarTable::solution: the SolveResult has no primal feasible solution.");
    }
    const auto& variable_values = result.variable_values();
    std::vector<double> values;
    values.reserve(this->vars_.size());
    for (const Variable& variable : this->vars_) {
      values.push_back(variable_values.at(variable));
    }
    return this->solutionTable(std::move(values));
  }
};

/// Returns the sum of `term` over `rows`, which is not built yet.
///
/// `build()` makes the sum into a `math_opt::LinearExpression`: the sum of `term(r)` over the rows `r`. If the term
/// returns a quadratic term or a quadratic expression, `build()` makes a `math_opt::QuadraticExpression` instead,
/// because MathOpt keeps the two kinds of expression apart. `sumExpr` takes no model, because an empty
/// `LinearExpression` needs none. The Sums section describes the rows, the term, `where` and `whereEach`.
///
/// Takes: If `rows` is a set or the rows of a group, `Columns` must be empty. If `rows` is a container, `Columns` must
/// be the one column that the container's elements fill.
/// Contract: column_integer_in_range
template <typename... Columns, typename Rows, typename Term>
[[nodiscard]] auto sumExpr(Rows&& rows, Term&& term) {
  return internal::makeSum<Columns...>([] { return ::operations_research::math_opt::LinearExpression{}; },
                                       std::forward<Rows>(rows),
                                       std::forward<Term>(term));
}

/// @hidden - exists only to reject, with a message, a model or an environment given first
template <typename... Columns, typename First, typename Rows, typename Term>
auto sumExpr(First&& /*first*/, Rows&& /*rows*/, Term&& /*term*/) {
  static_assert(internal::always_false<First>,
                "MathOpt's sumExpr takes no model and no environment: sumExpr(rows, term). An empty "
                "LinearExpression needs neither.");
}

}  // namespace dreamor::mathopt

// A term that MathOpt can hold only in a QuadraticExpression makes the sum a QuadraticExpression. A linear term, such
// as a variable, a LinearTerm, a LinearExpression or a number, keeps the sum a LinearExpression.
template <typename Term>
  requires(!std::convertible_to<Term, ::operations_research::math_opt::LinearExpression> &&
           std::convertible_to<Term, ::operations_research::math_opt::QuadraticExpression>)
struct dreamor::internal::SumTotal<::operations_research::math_opt::LinearExpression, Term> {
  using type = ::operations_research::math_opt::QuadraticExpression;
};
