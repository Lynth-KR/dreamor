// SPDX-License-Identifier: Apache-2.0
// The same lot-sizing model built on both backends - Concert (the stub, tests/stub) and
// MathOpt - must have the same variables and the same constraints: each constraint as
// lower <= sum(coef * variable) <= upper, variables identified by name, compared as a multiset.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>

#include "check.hpp"
#include "float_compare.hpp"
#include "production_planning.hpp"
#include "production_planning_mathopt.hpp"

namespace math_opt = ::operations_research::math_opt;

static std::string number(double x) {
  if (std::isinf(x)) return x > 0 ? "+inf" : "-inf";
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.9g", x + 0.0);  // + 0.0: -0 and 0 print alike
  return buf;
}

static std::string row(double lower, double upper, const std::map<std::string, double>& terms) {
  std::string s = number(lower) + " <= ";
  for (const auto& [name, coef] : terms) {
    if (!isExactlyZero(coef)) s += number(coef) + "*" + name + " ";
  }
  return s + "<= " + number(upper);
}

static std::multiset<std::string> concertRows(const IloModel& model) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  std::multiset<std::string> out;
  for (const auto* c : *model.cs_) {
    std::map<std::string, double> terms;
    for (const auto& [v, k] : c->terms) terms[v->name] += k;
    // sense: -1 is terms <= rhs, 0 is terms == rhs, 1 is terms >= rhs
    const double lower = c->sense >= 0 ? c->rhs : -kInf;
    const double upper = c->sense <= 0 ? c->rhs : kInf;
    out.insert(row(lower, upper, terms));
  }
  return out;
}

static std::multiset<std::string> mathoptRows(const math_opt::Model& model) {
  std::multiset<std::string> out;
  for (const math_opt::LinearConstraint& c : model.LinearConstraints()) {
    std::map<std::string, double> terms;
    for (const math_opt::Variable& v : model.RowNonzeros(c)) terms[std::string{v.name()}] += c.coefficient(v);
    out.insert(row(c.lower_bound(), c.upper_bound(), terms));
  }
  return out;
}

int main() {
  using namespace planning;
  for (const auto& [M, I, T] : {std::array<std::size_t, 3>{2, 3, 4},
                                std::array<std::size_t, 3>{3, 6, 5},
                                std::array<std::size_t, 3>{5, 12, 8}}) {
    const Instance in = Instance::make(M, I, T);

    IloEnv env;
    IloModel concertModel(env);
    const Model concert{env, in};
    concert.build(env, concertModel);

    math_opt::Model mathoptModel("lot sizing");
    const MathOptModel mathopt{mathoptModel, in};
    mathopt.build(mathoptModel);

    const auto a = concertRows(concertModel), b = mathoptRows(mathoptModel);
    DREAMOR_CHECK(!a.empty() && a.size() == b.size());
    DREAMOR_CHECK(a == b);
    if (a != b) {
      for (const auto& r : a)
        if (!b.contains(r)) {
          std::fprintf(stderr, "  only in concert: %s\n", r.c_str());
          break;
        }
      for (const auto& r : b)
        if (!a.contains(r)) {
          std::fprintf(stderr, "  only in mathopt: %s\n", r.c_str());
          break;
        }
    }
    DREAMOR_CHECK(concert.vYield.size() + concert.vSetup.size() + concert.vStock.size() ==
                  static_cast<std::size_t>(mathoptModel.num_variables()));
  }
  return report("cross_backend");
}
