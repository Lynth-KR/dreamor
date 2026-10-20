// SPDX-License-Identifier: Apache-2.0
// examples/production_planning.hpp against the plain-Concert version of the same model:
// every constraint, compared as a multiset with variables identified by name.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "check.hpp"
#include "float_compare.hpp"
#include "production_planning.hpp"
#include "production_planning_plain.hpp"

static std::multiset<std::string> constraints(const IloModel& model) {
  std::multiset<std::string> out;
  for (auto* c : *model.cs_) {
    std::map<std::string, double> terms;
    for (auto [v, k] : c->terms) terms[v->name] += k;
    std::string s = std::to_string(c->sense) + "|" + std::to_string(c->rhs) + "|";
    for (auto& [n, k] : terms)
      if (!isExactlyZero(k)) s += n + "*" + std::to_string(k) + ";";
    out.insert(s);
  }
  return out;
}

// On a mismatch, say how: both counts and up to five constraints found on one side only.
static void showDiff(const std::multiset<std::string>& lib, const std::multiset<std::string>& plain) {
  std::fprintf(stderr, "  library %zu constraints, plain %zu\n", lib.size(), plain.size());
  std::vector<std::string> onlyLib, onlyPlain;
  std::set_difference(lib.begin(), lib.end(), plain.begin(), plain.end(), std::back_inserter(onlyLib));
  std::set_difference(plain.begin(), plain.end(), lib.begin(), lib.end(), std::back_inserter(onlyPlain));
  for (std::size_t k = 0; k < onlyLib.size() && k < 5; ++k)
    std::fprintf(stderr, "  only in library: %s\n", onlyLib[k].c_str());
  for (std::size_t k = 0; k < onlyPlain.size() && k < 5; ++k)
    std::fprintf(stderr, "  only in plain:   %s\n", onlyPlain[k].c_str());
}

int main() {
  using namespace planning;
  for (auto [M, I, T] : {std::array<std::size_t, 3>{2, 3, 4},
                         std::array<std::size_t, 3>{3, 6, 5},
                         std::array<std::size_t, 3>{5, 12, 8}}) {
    const Instance in = Instance::make(M, I, T);
    IloEnv env;
    IloModel lib(env), plain(env);
    const Model m{env, in};
    m.build(env, lib);
    PlainModel p{env, in};
    p.build(env, plain, in);
    const auto a = constraints(lib), b = constraints(plain);
    DREAMOR_CHECK(!a.empty() && a.size() == b.size());
    DREAMOR_CHECK(a == b);
    if (a != b) showDiff(a, b);
    DREAMOR_CHECK(m.vYield.size() == p.yieldVars.size() && m.vStock.size() == p.stockVars.size());
  }
  return report("example");
}
