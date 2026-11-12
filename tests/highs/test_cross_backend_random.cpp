// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The cross-backend differential test (support/cross_backend.hpp) on HiGHS: each random model built on the Concert
// stub and three times on HiGHS, compared with the stub's. A HighsEnv collects what is written until its first
// highs() and writes straight into the Highs after, by other code (internal/highs/model.hpp), so each case is built
// once collected whole, once handed over before anything is written, and once handed over at a random step between
// a table, a row and the objective. The model is read out of Highs::getLp(), the sums by their columns' names.
//
// HiGHS itself leaves out of its matrix a coefficient of magnitude small_matrix_value (1e-9) or less, before the
// hand-over (passModel) and after (addRow); dreamor leaves out exact zeros alone. Tenths that nearly cancel, such as
// 0.6 - 0.4 - 0.2, leave such a coefficient in a row, so the stub's rows are compared without them.
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dreamor/highs.hpp"
#include "support/check.hpp"
#include "support/cross_backend.hpp"

namespace {

using cross_backend::Linear;
using cross_backend::Readout;
using cross_backend::Sense;

inline constexpr std::size_t kAtTheEnd = std::numeric_limits<std::size_t>::max();

class HighsSide {
 public:
  explicit HighsSide(std::size_t hand_over_at) : env_{silent()}, hand_over_at_{hand_over_at} {}

  [[nodiscard]] static double infinity() { return kHighsInf; }

  template <typename... Keys, typename Lb, typename Ub>
  [[nodiscard]] dreamor::highs::VarTable<Keys...> table(
      const dreamor::IndexSet<Keys...>& set, const Lb& lb, const Ub& ub, bool integer, std::string_view name) {
    return {env_, set, lb, ub, integer ? HighsVarType::kInteger : HighsVarType::kContinuous, name};
  }

  template <typename... Keys>
  [[nodiscard]] dreamor::highs::VarTable<Keys...> defaults(const dreamor::IndexSet<Keys...>& set) {
    return {env_, set};
  }

  template <typename Table, typename BoundsOf>
  void record(const Table& /*table*/, std::string_view /*name*/, const BoundsOf& /*bounds_of*/) {
    // the variables are read out of the model, and compared with the stub's
  }

  template <typename Rows, typename Term>
  [[nodiscard]] static auto sum(const Rows& rows, const Term& term) {
    return dreamor::highs::sumExpr(rows, term);
  }

  template <typename Column, typename Rows, typename Term>
  [[nodiscard]] static auto sumOf(const Rows& rows, const Term& term) {
    return dreamor::highs::sumExpr<Column>(rows, term);
  }

  void readSum(std::string label, const dreamor::highs::HighsLinearExpression& e) {
    out.labels.push_back(std::move(label));
    sums_.push_back(e);  // read by the columns' names once the model is in the Highs
  }

  void addRow(const dreamor::highs::HighsLinearExpression& e, Sense sense, double rhs, const std::string& name) {
    if (sense == Sense::kLess) {
      static_cast<void>(dreamor::highs::addConstr(env_, e <= rhs, name));
    } else if (sense == Sense::kGreater) {
      static_cast<void>(dreamor::highs::addConstr(env_, e >= rhs, name));
    } else {
      static_cast<void>(dreamor::highs::addConstr(env_, e == rhs, name));
    }
  }

  void objective(const dreamor::highs::HighsLinearExpression& e, bool maximize) {
    dreamor::highs::setObjective(env_, e, maximize ? ObjSense::kMaximize : ObjSense::kMinimize);
  }

  void step() {
    if (steps_++ == hand_over_at_) { static_cast<void>(env_.highs()); }
  }

  [[nodiscard]] Readout finish() {
    const Highs& highs = env_.highs();
    const HighsLp& lp  = highs.getLp();
    const auto columns = static_cast<std::size_t>(lp.num_col_);
    const auto name    = [&lp](HighsInt column) {
      const auto at = static_cast<std::size_t>(column);
      return at < lp.col_names_.size() ? lp.col_names_[at] : std::string{};
    };
    for (std::size_t j = 0; j < columns; ++j) {
      const bool integer = j < lp.integrality_.size() && lp.integrality_[j] == HighsVarType::kInteger;
      out.variables.push_back(cross_backend::Variable{
          name(static_cast<HighsInt>(j)),
          cross_backend::bound(lp.col_lower_[j]),
          cross_backend::bound(lp.col_upper_[j]),
          integer,
      });
    }
    for (const dreamor::highs::HighsLinearExpression& e : sums_) {
      std::vector<std::pair<std::string, double>> terms;
      terms.reserve(e.size());
      for (std::size_t t = 0; t < e.size(); ++t) {
        terms.emplace_back(name(e.indices()[t]), e.coefficients()[t]);
      }
      out.sums.push_back(Linear{cross_backend::merged(terms), e.constant()});
    }
    std::vector<HighsInt> index(columns + 1);
    std::vector<double> value(columns + 1);
    for (HighsInt r = 0; r < lp.num_row_; ++r) {
      double lower      = 0;
      double upper      = 0;
      HighsInt count    = 0;
      HighsInt nonzeros = 0;
      HighsInt start    = 0;
      highs.getRows(r, r, count, &lower, &upper, nonzeros, &start, index.data(), value.data());
      std::vector<std::pair<std::string, double>> terms;
      terms.reserve(static_cast<std::size_t>(nonzeros));
      for (std::size_t t = 0; std::cmp_less(t, nonzeros); ++t) {
        terms.emplace_back(name(index[t]), value[t]);
      }
      const auto at = static_cast<std::size_t>(r);
      out.rows.push_back(cross_backend::Row{
          .name  = at < lp.row_names_.size() ? lp.row_names_[at] : std::string{},
          .lower = cross_backend::bound(lower),
          .upper = cross_backend::bound(upper),
          .terms = cross_backend::merged(terms),
      });
    }
    std::vector<std::pair<std::string, double>> costs;
    costs.reserve(columns);
    for (std::size_t j = 0; j < columns; ++j) {
      costs.emplace_back(name(static_cast<HighsInt>(j)), lp.col_cost_[j]);
    }
    out.objective = Linear{cross_backend::merged(costs), lp.offset_};
    out.maximize  = lp.sense_ == ObjSense::kMaximize;
    return std::move(out);
  }

  Readout out;  // NOLINT(*-non-private-member-variables-in-classes): writeModel records the thrown exception here

 private:
  [[nodiscard]] static HighsOptions silent() {
    HighsOptions options;
    options.output_flag = false;
    return options;
  }

  dreamor::highs::HighsEnv env_;
  std::size_t hand_over_at_;
  std::size_t steps_ = 0;
  std::vector<dreamor::highs::HighsLinearExpression> sums_;
};

[[nodiscard]] bool compare(const cross_backend::Case& k, const Readout& stub) {
  Readout expected   = stub;
  const double small = HighsOptions{}.small_matrix_value;
  for (cross_backend::Row& r : expected.rows) {
    std::erase_if(r.terms, [small](const auto& term) { return std::abs(term.second) <= small; });
  }
  bool ok = true;
  for (const auto& [at, label] : {
           std::pair<std::size_t, const char*>{kAtTheEnd, "highs (collected)"},
           std::pair<std::size_t, const char*>{0, "highs (handed over first)"},
           std::pair<std::size_t, const char*>{k.hand_over_at, "highs (handed over midway)"},
       }) {
    HighsSide side{at};
    cross_backend::writeModel(side, k);
    ok = cross_backend::sameModel(expected, side.finish(), label) && ok;
  }
  return ok;
}

}  // namespace

int main() try { return cross_backend::run("highs_cross_backend_random", compare); } catch (...) {
  return ::uncaught();
}
