// SPDX-License-Identifier: Apache-2.0
// File: concert.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// The IBM ILOG Concert backend (CPLEX): dreamor::concert::VarTable and
// dreamor::concert::sumOf. Bounds, variable types, names and solutions are in Concert's own
// terms - IloEnv, ILOFLOAT / ILOINT / ILOBOOL, IloCplex - because the model around them is
// written in Concert anyway. A short alias reads well at the call site:
//
//     namespace concert = dreamor::concert;
//
// ---------------------------------------------------------------------------------------
// VarTable
//
// One decision variable of a mathematical model, defined over an index set - the C++
// counterpart of `var Flow {ARCS} >= 0, <= cap[i,j];`:
//
//     const auto sARCS = makeSet<FromCol, ToCol>(Nodes, Nodes).where(reachable).collect();
//
//     const VarTable<FromCol, ToCol> vUse{env, sARCS, 0, 1, ILOBOOL, "Use"};
//     const VarTable<FromCol, ToCol> vFlow{env, sARCS, 0, [](FromCol i, ToCol j) { return cap(i, j); },
//                                          ILOFLOAT, "Flow"};
//
// It is built once, from its set, and its structure never changes: which index tuples
// exist, their order and which variable each one has. To have other variables, build
// another table over another set. The variables themselves are ordinary Concert objects -
// tighten a bound with vFlow(row).setUB(0) as usual.
//
// lb and ub are each a number or a callable over the set's columns, under the same rule as
// where(): `[](OrderCol o) { return o->quantity(); }`. Names come out as
// "<name>_<key0>_<key1>...", using keyName() where provided; a null name leaves the
// variables unnamed. VarTable(semiContinuous, env, set, sclb, ub, ...) makes semi-
// continuous variables instead.
//
// Using it:
//
//     vFlow(i, j)                                the variable of a tuple (std::out_of_range if absent)
//     for (const auto& [row, flowVar] : vFlow)       every (row, variable), in the set's order
//     const auto flowPlan = vFlow.solution(cplex);   every value at once; flowPlan(i, j), range-for (row, value)
//
// One table per variable family, built on the family's whole set. A subset or a group of
// that set is not a new family: reach its variables with vFlow(row). Building a second
// table on a subset (VarTable{env, sARCS.where(p), ...} beside one on sARCS) creates a
// second, unrelated variable for every row of the subset - the library cannot tell that
// from two genuinely different families over one set (vUse and vFlow over sARCS), so it is
// this rule, not a check, that keeps it out.
//
// What it deliberately does NOT do: iterate a subset, slice or group. Which tuples exist,
// and which belong together, is the set's business - sARCS.where(...), sARCS.groupBy<...>() -
// and vFlow(row) turns any of those rows into its variable.
//
// Storage: the set is shared, not copied, so keys are stored once. Beside it sit the
// variables in the set's order and a hash index from key to row (internal/row_index.hpp),
// which every table on the same set shares - vUse and vFlow over sARCS build it once. Column
// order handed to the model therefore follows the set, reproducibly - pointer keys
// included.
//
// ---------------------------------------------------------------------------------------
// sumOf
//
// sum {t in ROWS : pred(t)} term(t)
//
//     sumOf(env, sASSIGN, term).build()                    -> IloExpr
//     sumOf(env, sASSIGN, term).where(p).where(q).build()  -> IloExpr, over rows where p AND q
//     sumOf(env, sASSIGN, term).split(p1, p2, p3).build()  -> std::array<IloExpr, 3>
//     sumOf<MachineCol>(env, machines, term).build()       -> IloExpr, over a plain container
//
// where() narrows, exactly as makeSet's does. split() is the other thing: one sum per
// predicate, sharing a single traversal - for a set several constraints read differently.
// The two compose: .where(common).split(a, b) gives two sums, both narrowed by common.
//
// ROWS is an index set - a range of column tuples. The term and every predicate name their
// columns in their own parameter list, the same way makeSet's callables do:
//
//     const auto is_early = [](ItemCol i) { return i->dueDate() < 100; };
//     const auto weighted_assign = [&vAssign](MachineCol m, ItemCol i) { return cost(m, i) * vAssign(m, i); };
//     sumOf(env, sASSIGN, weighted_assign).where(is_early).build();
//
// So parameter order is free, and a callable takes only the columns it needs. term() runs
// at most once per row even under split() - it is usually a VarTable lookup.
#pragma once

#include <concepts>
#include <cstddef>
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
#include <ilconcert/iloexpression.h>
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::concert {

// Tag for the semi-continuous constructor: VarTable(semiContinuous, env, set, sclb, ub, ...).
inline constexpr struct SemiContinuous {
} semiContinuous{};  // NOLINT(readability-identifier-naming): a public tag object, named like std::in_place

template <typename... Keys>
  requires(sizeof...(Keys) > 0 && (internal::is_nameable<Keys> && ...))
class VarTable : public internal::VarTableCore<IloNumVar, Keys...> {
  using Core = internal::VarTableCore<IloNumVar, Keys...>;

 public:
  using Key = Core::Key;
  // Lookup (vYield(m, i, t), vYield(row)), contains(), size(), keys() and iteration are the
  // core's; so is the constructor that stops on a set with other key columns. Copy is deleted
  // (IloNumVar is a handle: a copy would alias the same Concert columns under two names) and
  // move is fine; nothing is end()ed here - Concert objects follow their env.
  using Core::Core;

  template <typename Lb = IloNum, typename Ub = IloNum>
  VarTable(const IloEnv& env,
           const IndexSet<Key>& set,
           const Lb& lb         = 0,
           const Ub& ub         = IloInfinity,
           IloNumVar::Type type = ILOFLOAT,
           const char* name     = nullptr)
      : Core{set} {
    internal::checkBound<Lb, Key>();
    internal::checkBound<Ub, Key>();
    typename Core::NameBuilder names{name};
    this->vars_.reserve(this->rows().size());
    for (const Key& row : this->rows()) {
      this->vars_.emplace_back(
          env, internal::boundOf(lb, row), internal::boundOf(ub, row), type, Core::nameOf(names, row));
    }
  }

  template <typename Lb = IloNum, typename Ub = IloNum>
  VarTable(SemiContinuous /*unused*/,
           const IloEnv& env,
           const IndexSet<Key>& set,
           const Lb& sclb,
           const Ub& ub,
           IloNumVar::Type type = ILOFLOAT,
           const char* name     = nullptr)
      : Core{set} {
    internal::checkBound<Lb, Key>();
    internal::checkBound<Ub, Key>();
    typename Core::NameBuilder names{name};
    this->vars_.reserve(this->rows().size());
    for (const Key& row : this->rows()) {
      this->vars_.push_back(IloSemiContVar(
          env, internal::boundOf(sclb, row), internal::boundOf(ub, row), type, Core::nameOf(names, row)));
    }
  }

  // Every value at once, after a solve: one getValues() call for the whole table. The
  // result shares this table's set and index and owns plain doubles, so it outlives the
  // algorithm and the env. Algorithm is IloCplex (or anything with getEnv() and
  // getValues(IloNumArray, IloNumVarArray)).
  template <typename Algorithm>
  [[nodiscard]] ValueTable<Keys...> solution(const Algorithm& algorithm) const {
    std::vector<double> values(this->vars_.size());
    if (!this->vars_.empty()) {
      const IloEnv env = algorithm.getEnv();
      IloNumVarArray array(env);
      for (const IloNumVar& var : this->vars_) {
        array.add(var);
      }
      IloNumArray out(env);
      algorithm.getValues(out, array);
      for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = out[static_cast<IloInt>(i)];
      }
      out.end();  // Temporaries made here; ending the array does not end its variables.
      array.end();
    }
    return this->valueTable(std::move(values));
  }
};

// sum {t in rows} term(t) as an IloExpr; see the description at the top of this file.
template <typename... Columns, std::ranges::input_range Rows, typename Term>
[[nodiscard]] auto sumOf(IloEnv env, Rows&& rows, Term&& term) {
  return internal::makeSum<Columns...>(
      [env] { return IloExpr{env}; }, std::forward<Rows>(rows), std::forward<Term>(term));
}

// sumOf(rows, term) - without the environment, as MathOpt's takes it - stops on the fix.
template <typename... Columns, typename Rows, typename Term>
auto sumOf(Rows&& /*rows*/, Term&& /*term*/) {
  static_assert(internal::always_false<Rows>,
                "Concert's sumOf takes the IloEnv first: sumOf(env, rows, term). The one without an "
                "environment is MathOpt's, dreamor::mathopt::sumOf(rows, term).");
}

}  // namespace dreamor::concert
