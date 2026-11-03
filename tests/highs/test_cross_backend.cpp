// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The same lot-sizing model built on both backends - Concert (the stub, tests/stub) and
// HiGHS - must have the same variables and the same constraints: each constraint as
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
#include <utility>
#include <vector>

#include "check.hpp"
#include "float_compare.hpp"
#include "production_planning.hpp"
#include "production_planning_highs.hpp"

static std::string number(double x) {
  if (std::isinf(x)) { return x > 0 ? "+inf" : "-inf"; }
  std::array<char, 64> buf{};
  std::snprintf(buf.data(), buf.size(), "%.9g", x + 0.0);  // + 0.0: -0 and 0 print alike
  return buf.data();
}

static std::string row(double lower, double upper, const std::map<std::string, double>& terms) {
  std::string s = number(lower) + " <= ";
  for (const auto& [name, coef] : terms) {
    if (!isExactlyZero(coef)) { s += number(coef) + "*" + name + " "; }
  }
  return s + "<= " + number(upper);
}

static std::multiset<std::string> concertRows(const IloModel& model) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  std::multiset<std::string> out;
  for (const auto* c : *model.cs_) {
    std::map<std::string, double> terms;
    for (const auto& [v, k] : c->terms) {
      terms[v->name] += k;
    }
    // sense: -1 is terms <= rhs, 0 is terms == rhs, 1 is terms >= rhs
    const double lower = c->sense >= 0 ? c->rhs : -kInf;
    const double upper = c->sense <= 0 ? c->rhs : kInf;
    out.insert(row(lower, upper, terms));
  }
  return out;
}

static std::multiset<std::string> highsRows(const Highs& highs) {
  std::multiset<std::string> out;
  const auto columns = static_cast<std::size_t>(highs.getNumCol());
  std::vector<HighsInt> index(columns);
  std::vector<double> value(columns);
  for (HighsInt r = 0; r < highs.getNumRow(); ++r) {
    double lower      = 0;
    double upper      = 0;
    HighsInt count    = 0;
    HighsInt nonzeros = 0;
    HighsInt start    = 0;
    highs.getRows(r, r, count, &lower, &upper, nonzeros, &start, index.data(), value.data());
    std::map<std::string, double> terms;
    for (std::size_t k = 0; std::cmp_less(k, nonzeros); ++k) {
      std::string name;
      highs.getColName(index[k], name);
      terms[name] += value[k];
    }
    out.insert(row(lower, upper, terms));
  }
  return out;
}

int main() try {
  using namespace planning;
  for (const auto& [M, I, T] : {
           std::array<std::size_t, 3>{2, 3, 4},
           std::array<std::size_t, 3>{3, 6, 5},
           std::array<std::size_t, 3>{5, 12, 8},
       }) {
    const Instance in = Instance::make(M, I, T);

    const IloEnv env;
    const IloModel concertModel(env);
    const Model concert{env, in};
    concert.build(env, concertModel);

    dreamor::highs::HighsEnv highsEnv;
    const HighsApiModel highs{highsEnv, in};
    highs.build(highsEnv);

    const auto a = concertRows(concertModel);
    const auto b = highsRows(highsEnv.highs());
    DREAMOR_CHECK(!a.empty() && a.size() == b.size());
    DREAMOR_CHECK(a == b);
    if (a != b) {
      for (const auto& r : a) {
        if (!b.contains(r)) {
          std::fprintf(stderr, "  only in concert: %s\n", r.c_str());
          break;
        }
      }
      for (const auto& r : b) {
        if (!a.contains(r)) {
          std::fprintf(stderr, "  only in highs: %s\n", r.c_str());
          break;
        }
      }
    }
    DREAMOR_CHECK(concert.vYield.size() + concert.vSetup.size() + concert.vStock.size() ==
                  static_cast<std::size_t>(highsEnv.highs().getNumCol()));
  }
  return report("cross_backend");
} catch (...) { return ::uncaught(); }
