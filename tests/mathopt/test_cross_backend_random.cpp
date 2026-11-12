// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The cross-backend differential test (support/cross_backend.hpp) on MathOpt: each random model built on the Concert
// stub and on a math_opt::Model, compared with the stub's. The model is read out of the math_opt::Model, its
// variables and constraints in the order of their ids, which is the order they were added in.
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dreamor/mathopt.hpp"
#include "support/check.hpp"
#include "support/cross_backend.hpp"

namespace {

namespace math_opt = ::operations_research::math_opt;

using cross_backend::Linear;
using cross_backend::Readout;
using cross_backend::Sense;

[[nodiscard]] Linear linearOf(const math_opt::LinearExpression& e) {
  std::vector<std::pair<std::string_view, double>> terms;
  for (const auto& [v, k] : e.terms()) {
    terms.emplace_back(v.name(), k);
  }
  return {cross_backend::merged(terms), e.offset()};
}

class MathoptSide {
 public:
  [[nodiscard]] static double infinity() { return std::numeric_limits<double>::infinity(); }

  template <typename... Keys, typename Lb, typename Ub>
  [[nodiscard]] dreamor::mathopt::VarTable<Keys...> table(
      const dreamor::IndexSet<Keys...>& set, const Lb& lb, const Ub& ub, bool integer, std::string_view name) {
    return {model_, set, lb, ub, integer, name};
  }

  template <typename... Keys>
  [[nodiscard]] dreamor::mathopt::VarTable<Keys...> defaults(const dreamor::IndexSet<Keys...>& set) {
    return {model_, set};
  }

  template <typename Table, typename BoundsOf>
  void record(const Table& /*table*/, std::string_view /*name*/, const BoundsOf& /*bounds_of*/) {
    // the variables are read out of the model, and compared with the stub's
  }

  template <typename Rows, typename Term>
  [[nodiscard]] static auto sum(const Rows& rows, const Term& term) {
    return dreamor::mathopt::sumExpr(rows, term);
  }

  template <typename Column, typename Rows, typename Term>
  [[nodiscard]] static auto sumOf(const Rows& rows, const Term& term) {
    return dreamor::mathopt::sumExpr<Column>(rows, term);
  }

  void readSum(std::string label, const math_opt::LinearExpression& e) {
    out.labels.push_back(std::move(label));
    out.sums.push_back(linearOf(e));
  }

  void addRow(const math_opt::LinearExpression& e, Sense sense, double rhs, const std::string& name) {
    if (sense == Sense::kLess) {
      model_.AddLinearConstraint(e <= rhs, name);
    } else if (sense == Sense::kGreater) {
      model_.AddLinearConstraint(e >= rhs, name);
    } else {
      model_.AddLinearConstraint(e == rhs, name);
    }
  }

  void objective(const math_opt::LinearExpression& e, bool maximize) { model_.SetObjective(e, maximize); }

  void step() {}

  [[nodiscard]] Readout finish() {
    for (const math_opt::Variable& v : model_.SortedVariables()) {
      out.variables.push_back(cross_backend::Variable{
          std::string{v.name()},
          cross_backend::bound(v.lower_bound()),
          cross_backend::bound(v.upper_bound()),
          v.is_integer(),
      });
    }
    for (const math_opt::LinearConstraint& c : model_.SortedLinearConstraints()) {
      std::vector<std::pair<std::string_view, double>> terms;
      for (const math_opt::Variable& v : model_.RowNonzeros(c)) {
        terms.emplace_back(v.name(), c.coefficient(v));
      }
      out.rows.push_back(cross_backend::Row{
          .name  = std::string{c.name()},
          .lower = cross_backend::bound(c.lower_bound()),
          .upper = cross_backend::bound(c.upper_bound()),
          .terms = cross_backend::merged(terms),
      });
    }
    out.objective = linearOf(model_.ObjectiveAsLinearExpression());
    out.maximize  = model_.is_maximize();
    return std::move(out);
  }

  Readout out;  // NOLINT(*-non-private-member-variables-in-classes): writeModel records the thrown exception here

 private:
  math_opt::Model model_{"cross_backend_random"};
};

[[nodiscard]] bool compare(const cross_backend::Case& k, const Readout& stub) {
  MathoptSide side;
  cross_backend::writeModel(side, k);
  return cross_backend::sameModel(stub, side.finish(), "mathopt");
}

}  // namespace

int main() try { return cross_backend::run("mathopt_cross_backend_random", compare); } catch (...) {
  return ::uncaught();
}
