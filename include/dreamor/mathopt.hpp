// SPDX-License-Identifier: Apache-2.0
// File: mathopt.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// The OR-Tools MathOpt backend: dreamor::mathopt::VarTable and dreamor::mathopt::sumOf.
// MathOpt is one modeling API in front of several solvers (HiGHS, SCIP, GLOP, PDLP,
// CP-SAT, GLPK, Gurobi, Xpress, ...), so this one backend reaches all of them.
//
// Everything is in MathOpt's own terms, as concert.hpp is in Concert's:
//
//     namespace math_opt = ::operations_research::math_opt;
//     math_opt::Model model("lot sizing");
//     const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vYield{
//         model, sPRODUCE, 0, max_possible_qty, /*is_integer=*/false, "Yield"};
//     const dreamor::mathopt::VarTable<MachineCol, ItemCol, PeriodCol> vSetup{model, sPRODUCE, 0, 1, true, "Setup"};
//
//     math_opt::LinearExpression totalLoad = dreamor::mathopt::sumOf(rows, machine_usage).build();
//     model.AddLinearConstraint(totalLoad <= machine->capacity);
//
//     const absl::StatusOr<math_opt::SolveResult> result = math_opt::Solve(model, math_opt::SolverType::kHighs);
//     const auto yieldPlan = vYield.solution(*result);
//
// Differences from concert.hpp, all of them MathOpt's:
//   - the table is built on a math_opt::Model& (there is no environment), the variable kind
//     is MathOpt's `is_integer` flag, and the default upper bound is +infinity;
//   - sumOf takes no environment - an empty LinearExpression needs none - and build()
//     returns a math_opt::LinearExpression (std::array of them under split());
//   - solution() reads a math_opt::SolveResult and throws std::logic_error when it has no
//     primal feasible solution (MathOpt itself would CHECK-fail);
//   - no semi-continuous variables: MathOpt's model has none.
// The rest is the library's and the same everywhere: bounds as numbers or column callables,
// names "<n>_<key0>_<key1>...", vYield(row) throwing std::out_of_range for a row outside the set,
// iteration in the set's order, and sumOf's where() / split().
//
// Building against the OR-Tools prebuilt: define OR_PROTO_DLL as empty, add its include/,
// and link libortools, libprotobuf and the abseil libraries (tests/run_tests.sh --mathopt).
#pragma once

#include <concepts>
#include <cstddef>
#include <exception>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/index_set/indexed.hpp"
#include "dreamor/index_set/key_args.hpp"
#include "dreamor/internal/row_index.hpp"
#include "dreamor/internal/sum_builder.hpp"
#include "dreamor/internal/var_table_core.hpp"
#include "dreamor/value_table.hpp"

#include "dreamor/general/third_party_headers.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#include "ortools/math_opt/cpp/model.h"
#include "ortools/math_opt/cpp/solve_result.h"
#include "ortools/math_opt/cpp/variable_and_expressions.h"
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::mathopt {

template <typename... Keys>
  requires(sizeof...(Keys) > 0 && (internal::is_nameable<Keys> && ...))
class VarTable : private internal::VarTableCore<::operations_research::math_opt::Variable, Keys...> {
  using Core = internal::VarTableCore<::operations_research::math_opt::Variable, Keys...>;

 public:
  using Key      = Core::Key;
  using Variable = ::operations_research::math_opt::Variable;

  template <typename Lb = double, typename Ub = double>
  VarTable(::operations_research::math_opt::Model& model,
           const IndexSet<Key>& set,
           Lb lb            = 0,
           Ub ub            = std::numeric_limits<double>::infinity(),
           bool is_integer  = false,
           const char* name = nullptr)
      : Core{set} {
    internal::checkBound<Lb, Key>();
    internal::checkBound<Ub, Key>();
    typename Core::NameBuilder names{name};
    this->vars_.reserve(this->rows().size());
    for (const Key& row : this->rows()) {
      const char* const variable_name = Core::nameOf(names, row);
      this->vars_.push_back(model.AddVariable(internal::boundOf(lb, row),
                                              internal::boundOf(ub, row),
                                              is_integer,
                                              variable_name != nullptr ? variable_name : ""));
    }
  }

  // A set whose columns are not exactly the keys, in order: see concert::VarTable.
  template <typename OtherTup, typename... Rest>
    requires(!std::same_as<OtherTup, Key>)
  VarTable(::operations_research::math_opt::Model& /*model*/, const IndexSet<OtherTup>& /*set*/, Rest&&... /*rest*/)
      : Core{IndexSet<Key>{}} {
    static_assert(internal::always_false<OtherTup>,
                  "A VarTable is built on a set with exactly its key columns, in the same order: "
                  "VarTable<MachineCol, ItemCol, PeriodCol> takes a SetOf<MachineCol, ItemCol, PeriodCol>. "
                  "Declare the table in the set's order, or derive a set in the table's order with select<...>().");
  }

  // Copy is deleted (a Variable refers to the model's variable: a copy would alias it under
  // two names); move is fine. The variables belong to the model, not to this table.
  VarTable(const VarTable&)                = delete;
  VarTable& operator=(const VarTable&)     = delete;
  VarTable(VarTable&&) noexcept            = default;
  VarTable& operator=(VarTable&&) noexcept = default;
  ~VarTable() noexcept                     = default;

  // The variable of a key, vYield(m, i, t) or vYield(row), under the same rules as
  // concert::VarTable. Throws std::out_of_range when the set does not have the key (MathOpt
  // has no null Variable to return anyway).
  template <typename... Args>
  [[nodiscard]] Variable operator()(const Args&... key) const {
    if constexpr (internal::checkTableKey<Key, Args...>()) {
      return this->vars_[this->rowOf(key...)];
    } else {
      std::terminate();
    }
  }

  using Core::begin;
  using Core::contains;
  using Core::end;
  using Core::keys;
  using Core::size;

  // Every value of the best primal feasible solution, keyed like this table. The result owns
  // plain doubles, so it outlives the model and the SolveResult.
  [[nodiscard]] ValueTable<Keys...> solution(const ::operations_research::math_opt::SolveResult& result) const {
    if (!result.has_primal_feasible_solution()) {
      throw std::logic_error("VarTable::solution: the SolveResult has no primal feasible solution.");
    }
    const auto& variable_values = result.variable_values();
    std::vector<double> values;
    values.reserve(this->vars_.size());
    for (const Variable& variable : this->vars_) {
      values.push_back(variable_values.at(variable));
    }
    return this->valueTable(std::move(values));
  }
};

// sum {t in rows} term(t) as a math_opt::LinearExpression; see concert.hpp for where(),
// split() and the rules on rows and callables, which are the same here.
template <typename... Columns, std::ranges::input_range Rows, typename Term>
[[nodiscard]] auto sumOf(Rows&& rows, Term&& term) {
  return internal::makeSum<Columns...>([] { return ::operations_research::math_opt::LinearExpression{}; },
                                       std::forward<Rows>(rows),
                                       std::forward<Term>(term));
}

// sumOf(model, rows, term) or sumOf(env, rows, term) - with a first argument, as VarTable and
// Concert's sumOf take one - stops on the fix.
template <typename... Columns, typename First, typename Rows, typename Term>
auto sumOf(First&& /*first*/, Rows&& /*rows*/, Term&& /*term*/) {
  static_assert(internal::always_false<First>,
                "MathOpt's sumOf takes no model and no environment: sumOf(rows, term). An empty "
                "LinearExpression needs neither.");
}

}  // namespace dreamor::mathopt

// A table is a term, as in concert.hpp: sumOf(rows, vYield).
template <typename... Keys>
struct dreamor::PredicateColumns<dreamor::mathopt::VarTable<Keys...>> {
  using type = std::tuple<Keys...>;
};
