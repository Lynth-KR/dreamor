// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The visit-order model: the planner's order against the other orders, over made-up sets of 3 to 16 sources.
//
// The question is how good the planner's order is over many made-up sets, not over a few written by hand. The planner
// (plan.hpp) is a constexpr function of column masks: the columns that each source binds, the columns that each source
// reads, and the columns that each predicate reads. So this program calls the planner at run time on random sets, and
// sets its order against the other orders that the dependencies allow. No set is built. An order's cost is the work
// that its nested loops do, in expectation.
//
// The cost: a source visited after the sources in S runs once for each partial tuple that survives S.
//   T(S)      = the product of the sizes in S x the product of the selectivities of the predicates that S binds
//   cost(ord) = the sum over the steps of T(the sources before the step) x size(the step's source)
// The cost counts the candidates that the loops look at, each of which is bound and checked. T depends on the set,
// not on the order, so it is worked out once for each subset. A dynamic program over the subsets then finds the best
// and the worst order. The program is exact at 16 sources, where listing the 16! orders is not possible. The size of a
// dependent source is the length of its list for one binding, and it multiplies in the same way.
//
// The planner sees the names of the columns only: the columns that a predicate or a source reads. It does not see the
// sizes or the selectivities, which this program draws.
//
// For each set, the planner's order is set against these orders:
//   without rule 4  plan.hpp's rules, with declaration order breaking every tie that the predicates leave. A
//                   dependent source counts the same as a container, so the difference is what rule 4 is worth.
//   declared        The order in which the sources are written.
//   sizes 1/10      A greedy order that knows the sizes but not the selectivities, and takes each predicate to keep
//                   1/10. It shows what a choice by size alone, at run time, could do.
//   + sizes         plan.hpp's rules, with the sizes breaking the ties that the rules leave. It shows what run time
//                   could add without a guess at the selectivities. The library does not implement it.
//   oracle          The greedy order that knows the sizes and the selectivities, and visits next the source after
//                   which the fewest partial tuples survive.
// Each order's regret is its cost over the best order's cost. The planner's rank among the orders runs from 0, the
// best, to 1, the worst. Up to 8 sources, the rank is over every order; above 8, over a sample of the orders.
//
// The list of a dependent source is drawn shorter than a container: 2 to 50 elements, against 10 to 1000 rows. The
// planner's rule that prefers a dependent source rests on this assumption. --long-lists draws both from the same
// range, to show what the rule costs when the assumption is wrong.
//
//   model [--sets N] [--ks 3,4,...] [--seed S] [--long-lists]   The table, with N sets in each row.
//   model --check                                                The checks of the model: the cost model against
//                                                                real loops, the dynamic program against every
//                                                                order, and this file's copy of the rules against
//                                                                plan.hpp.
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dreamor/internal/index_set/plan.hpp"

namespace {

constexpr std::size_t kMaxSources = 16;
// A clique on 16 columns has 120 predicates, and a filter on each column adds 16.
constexpr std::size_t kMaxPreds = 136;
// Up to this number of sources, the rank is over every order. Above it, the rank is over kSampled orders.
constexpr std::size_t kExactUpTo = 8;
constexpr std::size_t kSampled   = 4000;
// If --long-lists is given, a dependent list is drawn as long as a container.
bool g_long_lists = false;

struct Pred {
  std::uint64_t mask;
  // The share of the rows that the predicate sees that pass it.
  double keeps;
};

// One set. Source i binds column i, and deps[i] is the mask of the columns that source i reads. A source whose
// deps[i] is 0 is a container.
struct Query {
  std::size_t k = 0;
  std::array<double, kMaxSources> size{};
  std::array<std::uint64_t, kMaxSources> deps{};
  std::vector<Pred> preds;
};

using Order = std::vector<std::size_t>;

// Returns the planner's own order: planVisitOrder over the masks that a chain's build() would pass to it.
template <std::size_t K>
Order plannedFixed(const Query& q) {
  std::array<std::uint64_t, K> produces{};
  std::array<std::uint64_t, K> depends{};
  // An unused predicate is 0. It reads no column, so it has no effect.
  std::array<std::uint64_t, kMaxPreds> preds{};
  for (std::size_t i = 0; i < K; ++i) {
    produces[i] = std::uint64_t{1} << i;
    depends[i]  = q.deps[i];
  }
  for (std::size_t p = 0; p < q.preds.size(); ++p) {
    preds[p] = q.preds[p].mask;
  }
  const auto order = dreamor::internal::planVisitOrder<K, kMaxPreds>(produces, depends, preds);
  return {order.begin(), order.end()};
}

template <std::size_t... Ks>
Order plannedAny(const Query& q, std::index_sequence<Ks...> /*unused*/) {
  Order out;
  static_cast<void>(((q.k == Ks + 1 ? (out = plannedFixed<Ks + 1>(q), true) : false) || ...));
  return out;
}

Order planned(const Query& q) {
  return plannedAny(q, std::make_index_sequence<kMaxSources>{});
}

// Returns T(S) for every subset S. T(S) is T of S without its lowest source, times the size of that source and the
// selectivities of the predicates that the source completes.
std::vector<double> survivors(const Query& q) {
  std::vector<std::vector<const Pred*>> with(q.k);
  for (const Pred& p : q.preds) {
    for (std::size_t i = 0; i < q.k; ++i) {
      if ((p.mask >> i) & 1U) { with[i].push_back(&p); }
    }
  }
  std::vector<double> t(std::size_t{1} << q.k, 1.0);
  for (std::uint64_t s = 1; s < t.size(); ++s) {
    const auto low = static_cast<std::size_t>(std::countr_zero(s));
    double v       = t[s & (s - 1)] * q.size[low];
    for (const Pred* p : with[low]) {
      if ((p->mask & ~s) == 0) { v *= p->keeps; }
    }
    t[s] = v;
  }
  return t;
}

double costOf(const Query& q, const std::vector<double>& t, const Order& order) {
  double cost       = 0;
  std::uint64_t set = 0;
  for (const std::size_t s : order) {
    cost += t[set] * q.size[s];
    set |= std::uint64_t{1} << s;
  }
  return cost;
}

// Returns the cost of the cheapest and of the dearest order, by a dynamic program over the subsets.
std::pair<double, double> bestAndWorst(const Query& q, const std::vector<double>& t) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  std::vector<double> lo(t.size(), kInf), hi(t.size(), -kInf);
  lo[0] = hi[0] = 0;
  for (std::uint64_t s = 1; s < t.size(); ++s) {
    for (std::size_t i = 0; i < q.k; ++i) {
      const std::uint64_t before = s & ~(std::uint64_t{1} << i);
      if (before == s || (q.deps[i] & ~before) != 0 || lo[before] == kInf) { continue; }
      const double step = t[before] * q.size[i];
      lo[s]             = std::min(lo[s], lo[before] + step);
      hi[s]             = std::max(hi[s], hi[before] + step);
    }
  }
  return {lo.back(), hi.back()};
}

// Appends the cost of every order that the dependencies allow, starting from the sources in set.
void everyOrder(
    const Query& q, const std::vector<double>& t, std::uint64_t set, double cost, std::vector<double>& out) {
  if (set == (std::uint64_t{1} << q.k) - 1) {
    out.push_back(cost);
    return;
  }
  for (std::size_t s = 0; s < q.k; ++s) {
    if (((set >> s) & 1U) != 0 || (q.deps[s] & ~set) != 0) { continue; }
    everyOrder(q, t, set | (std::uint64_t{1} << s), cost + t[set] * q.size[s], out);
  }
}

// Returns the sorted costs of every order if the set has at most kExactUpTo sources, or of kSampled random orders
// otherwise.
std::vector<double> orderCosts(const Query& q, const std::vector<double>& t, std::mt19937_64& g) {
  std::vector<double> out;
  if (q.k <= kExactUpTo) {
    everyOrder(q, t, 0, 0, out);
  } else {
    // At each step, a random order picks one of the sources whose dependencies are bound.
    for (std::size_t n = 0; n < kSampled; ++n) {
      Order order;
      std::uint64_t set = 0;
      for (std::size_t step = 0; step < q.k; ++step) {
        std::vector<std::size_t> ready;
        for (std::size_t s = 0; s < q.k; ++s) {
          if (((set >> s) & 1U) == 0 && (q.deps[s] & ~set) == 0) { ready.push_back(s); }
        }
        const std::size_t s = ready[std::uniform_int_distribution<std::size_t>(0, ready.size() - 1)(g)];
        order.push_back(s);
        set |= std::uint64_t{1} << s;
      }
      out.push_back(costOf(q, t, order));
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

// Returns the greedy order over t, which may be guessed: next, the source after which the fewest partial tuples
// survive.
Order bySize(const Query& q, const std::vector<double>& t) {
  Order order;
  std::uint64_t set = 0;
  for (std::size_t step = 0; step < q.k; ++step) {
    std::size_t best = q.k;
    for (std::size_t s = 0; s < q.k; ++s) {
      if (((set >> s) & 1U) != 0 || (q.deps[s] & ~set) != 0) { continue; }
      if (best == q.k || t[set | (std::uint64_t{1} << s)] < t[set | (std::uint64_t{1} << best)]) { best = s; }
    }
    order.push_back(best);
    set |= std::uint64_t{1} << best;
  }
  return order;
}

// Returns T with each predicate taken to keep `guess`: what run time could know, the sizes but not the selectivities.
std::vector<double> survivorsGuessed(Query q, double guess) {
  for (Pred& p : q.preds) {
    p.keeps = guess;
  }
  return survivors(q);
}

// Planners that do not exist. Each one applies plan.hpp's rules, the most predicates run and then the smallest gap,
// and breaks the ties that are left in another way:
//   - Without kTieDependent, declaration order alone breaks a tie, so rule 4 is left out.
//   - With kTieDependent, a dependent source comes before a container.
//   - With kTieSize, the smaller source comes next, which only run time can know.
// The library's planner, planned(), is kTieDependent alone. namesThen reproduces it, and model --check compares the
// two.
enum TieRule : unsigned { kTieSize = 1, kTieDependent = 2 };

Order namesThen(const Query& q, unsigned rules) {
  // before(a, b) is true if the tie rules put a before b.
  const auto before = [&](std::size_t a, std::size_t b) {
    if ((rules & kTieDependent) && (q.deps[a] != 0) != (q.deps[b] != 0)) { return q.deps[a] != 0; }
    if ((rules & kTieSize) && q.size[a] != q.size[b]) { return q.size[a] < q.size[b]; }
    // If no rule decides, declaration order does: the source met first stays.
    return false;
  };
  Order order;
  std::uint64_t bound = 0;
  for (std::size_t step = 0; step < q.k; ++step) {
    std::size_t best = q.k, best_activated = 0, best_gap = 0;
    for (std::size_t s = 0; s < q.k; ++s) {
      if (((bound >> s) & 1U) != 0 || (q.deps[s] & ~bound) != 0) { continue; }
      const std::uint64_t next = bound | (std::uint64_t{1} << s);
      std::size_t activated = 0, gap = ~std::size_t{0};
      for (const Pred& p : q.preds) {
        const std::uint64_t missing = p.mask & ~next;
        if (missing == 0) {
          activated += (p.mask & ~bound) != 0;
        } else {
          gap = std::min(gap, static_cast<std::size_t>(std::popcount(missing)));
        }
      }
      if (gap == ~std::size_t{0}) { gap = 0; }
      const bool better = best == q.k || activated > best_activated ||
                          (activated == best_activated && (gap < best_gap || (gap == best_gap && before(s, best))));
      if (better) {
        best           = s;
        best_activated = activated;
        best_gap       = gap;
      }
    }
    order.push_back(best);
    bound |= std::uint64_t{1} << best;
  }
  return order;
}

// A shape says which pairs of columns the predicates read. makeQuery shuffles the columns, so that no shape lines up
// with declaration order, which breaks the planner's last tie.
const std::vector<std::string_view> kShapes = {"chain", "star", "cycle", "clique", "random", "filters"};

double logUniform(std::mt19937_64& g, double lo, double hi) {
  return std::exp(std::uniform_real_distribution<double>(std::log(lo), std::log(hi))(g));
}

Query makeQuery(std::mt19937_64& g, std::size_t k, std::string_view shape, bool deps) {
  Query q;
  q.k = k;
  std::vector<std::size_t> label(k);
  std::iota(label.begin(), label.end(), 0);
  std::shuffle(label.begin(), label.end(), g);
  const auto pair = [&](std::size_t a, std::size_t b) {
    return (std::uint64_t{1} << label[a]) | (std::uint64_t{1} << label[b]);
  };
  std::vector<std::uint64_t> masks;
  if (shape == "chain" || shape == "cycle") {
    for (std::size_t i = 0; i + 1 < k; ++i) {
      masks.push_back(pair(i, i + 1));
    }
  }
  if (shape == "cycle") { masks.push_back(pair(k - 1, 0)); }
  if (shape == "star") {
    for (std::size_t i = 1; i < k; ++i) {
      masks.push_back(pair(0, i));
    }
  }
  if (shape == "clique") {
    for (std::size_t i = 0; i < k; ++i) {
      for (std::size_t j = i + 1; j < k; ++j) {
        masks.push_back(pair(i, j));
      }
    }
  }
  if (shape == "random") {
    // If k is 8 or more, the expected number of predicates is 1.5 (k - 1), about 1.5 for each column. If k is
    // smaller, the probability is capped at 0.4, which gives fewer predicates than that.
    const double p = std::min(0.4, 3.0 / static_cast<double>(k));
    for (std::size_t i = 0; i < k; ++i) {
      for (std::size_t j = i + 1; j < k; ++j) {
        if (std::bernoulli_distribution(p)(g)) { masks.push_back(pair(i, j)); }
      }
    }
  }
  if (shape == "filters") {
    for (std::size_t i = 0; i < k; ++i) {
      if (std::bernoulli_distribution(0.6)(g)) { masks.push_back(std::uint64_t{1} << i); }
    }
    // One predicate reads two columns, so that the set is not filtering alone.
    masks.push_back(pair(0, 1));
  }
  for (const std::uint64_t m : masks) {
    q.preds.push_back({m, logUniform(g, 0.01, 1.0)});
  }
  // A dependent source reads one column declared before it, as a machine's list of items reads the machine. A
  // container's size is its number of rows. A dependent source's size is the length of its list for one binding.
  for (std::size_t i = 0; i < k; ++i) {
    if (deps && i > 0 && std::bernoulli_distribution(0.4)(g)) {
      q.deps[i] = std::uint64_t{1} << std::uniform_int_distribution<std::size_t>(0, i - 1)(g);
      q.size[i] = g_long_lists ? logUniform(g, 10, 1000) : logUniform(g, 2, 50);
    } else {
      q.size[i] = logUniform(g, 10, 1000);
    }
  }
  return q;
}

double quantile(std::vector<double> v, double at) {
  std::sort(v.begin(), v.end());
  return v[static_cast<std::size_t>(at * static_cast<double>(v.size() - 1))];
}

struct Column {
  const char* name;
  std::vector<double> all;
};

int table(std::size_t sets, const std::vector<std::size_t>& ks, std::uint64_t seed) {
  std::mt19937_64 g(seed);
  std::printf("%zu sets per row. regret = cost / best order's (median); rank: 0 best .. 1 worst.\n", sets);
  std::printf("%-3s %-8s %-4s | %-24s %-5s %-5s | %-9s %-9s %-9s %-9s %-9s\n",
              "k",
              "shape",
              "deps",
              "planner regret med/p90",
              "<=1.2",
              "rank",
              "no rule 4",
              "declared",
              "sizes1/10",
              "+sizes",
              "oracle");
  std::vector<Column> overall = {{"planner", {}},
                                 {"without rule 4", {}},
                                 {"declared", {}},
                                 {"sizes, keeps 1/10", {}},
                                 {"planner + sizes", {}},
                                 {"oracle (sizes + sel.)", {}}};
  for (const std::size_t k : ks) {
    for (const std::string_view shape : kShapes) {
      for (const bool deps : {false, true}) {
        std::vector<std::vector<double>> r(overall.size());
        std::vector<double> rank;
        for (std::size_t n = 0; n < sets; ++n) {
          const Query q               = makeQuery(g, k, shape, deps);
          const std::vector<double> t = survivors(q);
          const double best           = bestAndWorst(q, t).first;
          const double mine           = costOf(q, t, planned(q));
          Order decl(k);
          std::iota(decl.begin(), decl.end(), 0);
          const double regrets[] = {mine / best,
                                    costOf(q, t, namesThen(q, 0)) / best,
                                    costOf(q, t, decl) / best,
                                    costOf(q, t, bySize(q, survivorsGuessed(q, 0.1))) / best,
                                    costOf(q, t, namesThen(q, kTieDependent | kTieSize)) / best,
                                    costOf(q, t, bySize(q, t)) / best};
          for (std::size_t c = 0; c < overall.size(); ++c) {
            r[c].push_back(regrets[c]);
          }
          // The planner's order is one of the orders that cost the same, so its rank is the middle of their ranks.
          const std::vector<double> costs = orderCosts(q, t, g);
          const auto below = std::lower_bound(costs.begin(), costs.end(), mine * (1 - 1e-12)) - costs.begin();
          const auto upto  = std::upper_bound(costs.begin(), costs.end(), mine * (1 + 1e-12)) - costs.begin();
          rank.push_back(costs.size() == 1 ? 0
                                           : std::clamp((static_cast<double>(below + upto) - 1) / 2,
                                                        0.0,
                                                        static_cast<double>(costs.size() - 1)) /
                                                 static_cast<double>(costs.size() - 1));
        }
        const auto within =
            static_cast<double>(std::count_if(r[0].begin(), r[0].end(), [](double x) { return x <= 1.2; }));
        std::printf("%-3zu %-8s %-4s | %9.2f %12.2f   %4.0f%% %5.2f | %9.2f %9.2f %9.2f %9.2f %9.2f\n",
                    k,
                    std::string(shape).c_str(),
                    deps ? "yes" : "no",
                    quantile(r[0], 0.5),
                    quantile(r[0], 0.9),
                    100 * within / static_cast<double>(sets),
                    quantile(rank, 0.5),
                    quantile(r[1], 0.5),
                    quantile(r[2], 0.5),
                    quantile(r[3], 0.5),
                    quantile(r[4], 0.5),
                    quantile(r[5], 0.5));
        for (std::size_t c = 0; c < overall.size(); ++c) {
          overall[c].all.insert(overall[c].all.end(), r[c].begin(), r[c].end());
        }
      }
    }
  }
  std::printf("all sets, regret:\n");
  for (const Column& c : overall) {
    std::printf("  %-22s median %7.2f  p90 %10.2f  p99 %12.2f\n",
                c.name,
                quantile(c.all, 0.5),
                quantile(c.all, 0.9),
                quantile(c.all, 0.99));
  }
  return 0;
}

std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

// Returns the number of candidates that real nested loops look at, to check costOf against them. check() gives it small
// sets with whole sizes. A predicate keeps a hash's 1 in round(1 / keeps), and each predicate runs as soon as its
// columns are bound, as in build().
std::uint64_t loops(const Query& q, const Order& order) {
  std::array<std::uint64_t, kMaxSources> value{};
  std::uint64_t looked                                  = 0;
  std::function<void(std::size_t, std::uint64_t)> visit = [&](std::size_t step, std::uint64_t set) {
    if (step == q.k) { return; }
    const std::size_t s = order[step];
    const auto n        = static_cast<std::uint64_t>(q.size[s]);
    for (std::uint64_t v = 0; v < n; ++v) {
      ++looked;
      // A dependent source's list holds the same elements whatever the binding.
      value[s]                 = v;
      const std::uint64_t next = set | (std::uint64_t{1} << s);
      bool passes              = true;
      for (std::size_t p = 0; p < q.preds.size() && passes; ++p) {
        const Pred& pr = q.preds[p];
        if ((pr.mask & ~next) != 0 || (pr.mask & ~set) == 0) {
          // The predicate cannot run yet, or it ran already.
          continue;
        }
        std::uint64_t h = p * 0x51afd7ed558ccd1ULL;
        for (std::size_t c = 0; c < q.k; ++c) {
          if ((pr.mask >> c) & 1U) { h = mix(h ^ (value[c] * 0x9e3779b97f4a7c15ULL + c)); }
        }
        passes = h % static_cast<std::uint64_t>(std::llround(1 / pr.keeps)) == 0;
      }
      if (passes) { visit(step + 1, next); }
    }
  };
  visit(0, 0);
  return looked;
}

int check(std::uint64_t seed) {
  std::mt19937_64 g(seed);
  std::vector<double> ratio;
  std::size_t dp_mismatch      = 0;
  std::size_t replica_mismatch = 0;
  for (std::size_t n = 0; n < 300; ++n) {
    const std::size_t k = std::uniform_int_distribution<std::size_t>(3, 5)(g);
    Query q             = makeQuery(g, k, kShapes[n % kShapes.size()], n % 2 == 1);
    for (std::size_t i = 0; i < k; ++i) {
      q.size[i] = std::round(std::min(q.size[i], q.deps[i] ? 12.0 : 40.0));
    }
    for (Pred& p : q.preds) {
      p.keeps = 1 / std::round(1 / std::max(p.keeps, 0.05));
    }
    const std::vector<double> t = survivors(q);
    // plan.hpp's order against this file's copy of its rules.
    replica_mismatch += planned(q) != namesThen(q, kTieDependent);
    for (const auto& order : {planned(q), bySize(q, t)}) {
      ratio.push_back(static_cast<double>(loops(q, order)) / costOf(q, t, order));
    }
    // The dynamic program over the subsets against a list of every order.
    std::vector<double> costs;
    everyOrder(q, t, 0, 0, costs);
    const auto [lo, hi] = bestAndWorst(q, t);
    const auto [mn, mx] = std::minmax_element(costs.begin(), costs.end());
    dp_mismatch += std::abs(lo / *mn - 1) > 1e-9 || std::abs(hi / *mx - 1) > 1e-9;
  }
  std::printf("real candidates / modelled, %zu orders: median %.3f, p5 %.3f, p95 %.3f\n",
              ratio.size(),
              quantile(ratio, 0.5),
              quantile(ratio, 0.05),
              quantile(ratio, 0.95));
  std::printf("best and worst over subsets against every order listed: %zu mismatches in 300 sets\n", dp_mismatch);
  std::printf("plan.hpp's order against this file's copy of its rules: %zu mismatches in 300 sets\n", replica_mismatch);
  return dp_mismatch == 0 && replica_mismatch == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t sets   = 200;
  std::uint64_t seed = 20261024;
  std::vector<std::size_t> ks{3, 4, 5, 6, 8, 10, 12, 14, 16};
  bool check_model = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    if (a == "--check") {
      check_model = true;
    } else if (a == "--long-lists") {
      g_long_lists = true;
    } else if (a == "--sets" && i + 1 < argc) {
      sets = std::stoul(argv[++i]);
    } else if (a == "--seed" && i + 1 < argc) {
      seed = std::stoull(argv[++i]);
    } else if (a == "--ks" && i + 1 < argc) {
      ks.clear();
      std::stringstream list(argv[++i]);
      for (std::string item; std::getline(list, item, ',');) {
        ks.push_back(std::stoul(item));
      }
    } else {
      std::fprintf(stderr, "usage: model [--sets N] [--ks 3,4,...] [--seed S] [--long-lists] | model --check\n");
      return 2;
    }
  }
  for (const std::size_t k : ks) {
    if (k < 1 || k > kMaxSources) {
      std::fprintf(stderr, "--ks: 1 to %zu sources\n", kMaxSources);
      return 2;
    }
  }
  return check_model ? check(seed) : table(sets, ks, seed);
}
