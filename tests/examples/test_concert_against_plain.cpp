// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The example's model against the same model written by hand: examples/production_planning_concert.hpp, built with
// dreamor, and examples/production_planning_plain.hpp, with std::maps and loops, give the same constraints at three
// sizes on the Concert stub, compared as multisets with the variables identified by name. The backends' cross tests
// compare dreamor's backends with each other; this test is the one that would see a mistake that they share.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "production_planning_concert.hpp"
#include "production_planning_plain.hpp"
#include "support/check.hpp"
#include "support/float_compare.hpp"

static std::multiset<std::string> constraints(const IloModel& model) {
  std::multiset<std::string> out;
  for (const auto* c : *model.cs_) {
    std::map<std::string, double> terms;
    for (auto [v, k] : c->terms) {
      terms[v->name] += k;
    }
    std::string s = std::to_string(c->sense) + "|" + std::to_string(c->rhs) + "|";
    for (auto& [n, k] : terms) {
      if (!isExactlyZero(k)) { s += n + "*" + std::to_string(k) + ";"; }
    }
    out.insert(s);
  }
  return out;
}

// On a mismatch, say how: both counts and up to five constraints found on one side only.
static void showDiff(const std::multiset<std::string>& lib, const std::multiset<std::string>& plain) {
  std::fprintf(stderr, "  library %zu constraints, plain %zu\n", lib.size(), plain.size());
  std::vector<std::string> onlyLib;
  std::vector<std::string> onlyPlain;
  std::ranges::set_difference(lib, plain, std::back_inserter(onlyLib));
  std::ranges::set_difference(plain, lib, std::back_inserter(onlyPlain));
  for (std::size_t k = 0; k < onlyLib.size() && k < 5; ++k) {
    std::fprintf(stderr, "  only in library: %s\n", onlyLib[k].c_str());
  }
  for (std::size_t k = 0; k < onlyPlain.size() && k < 5; ++k) {
    std::fprintf(stderr, "  only in plain:   %s\n", onlyPlain[k].c_str());
  }
}

int main() try {
  using namespace planning;
  for (auto [M, I, T] : {
           std::array<int, 3>{2, 3, 4},
           std::array<int, 3>{3, 6, 5},
           std::array<int, 3>{5, 12, 8},
       }) {
    const Instance in = Instance::make(M, I, T);
    const IloEnv env;
    const IloModel lib(env);
    const IloModel plain(env);
    const concert::Model m{env, in};
    m.build(env, lib);
    PlainModel p{env, in};
    p.build(env, plain, in);
    const auto a = constraints(lib);
    const auto b = constraints(plain);
    DREAMOR_CHECK(!a.empty() && a.size() == b.size());
    DREAMOR_CHECK(a == b);
    if (a != b) { showDiff(a, b); }
    DREAMOR_CHECK(m.vYield.size() == p.yieldVars.size() && m.vStock.size() == p.stockVars.size());
  }
  return report("example");
} catch (...) { return ::uncaught(); }
