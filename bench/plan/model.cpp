// SPDX-License-Identifier: Apache-2.0
// File: bench/plan/model.cpp
//
// How good is the order makeSet's planner picks, over many made-up sets rather than a handful
// of hand-written ones? The planner (plan.hpp) is a constexpr function of column masks - what
// each source binds, what it reads, what each predicate reads - so this calls it at run time on
// random sets of 3 to 16 sources and sets its order against the others the dependencies allow.
// No set is built: an order's cost is the work its nested loops do, in expectation.
//
// Cost. A source visited after the sources in S runs once per partial tuple that survived S:
//   T(S)      = product of the sizes in S x product of the selectivities of the predicates S binds
//   cost(ord) = sum over steps of T(sources before it) x size(the source)
// the candidates the loops look at - each one bound and checked. T depends on the set, not the
// order, so it is worked out once per subset; the best and worst orders then come from a
// dynamic program over subsets, which is exact at 16 sources where listing 16! orders is not. A
// dependent source's size is its list's length per binding, which multiplies the same way.
//
// What the planner sees: column names only - which columns a predicate or a source reads. Not
// sizes, not selectivities. The sizes and selectivities here are what it does not know.
//
// Against it, per set:
//   without rule 4  plan.hpp's rules with declaration order breaking every tie the predicates
//                   leave, a dependent source no different from a container: what rule 4 is worth
//   declared        the order the sources are written in
//   sizes 1/10      a greedy that knows the sizes, not the selectivities - each predicate taken to
//                   keep 1/10: what choosing by size alone at run time could do
//   + sizes         plan.hpp's rules with the sizes breaking the ties they leave: what run time
//                   could add without guessing selectivities (not implemented)
//   oracle          the greedy that knows sizes and selectivities: the fewest partial tuples next
// regret = cost / best order's cost, and the planner's rank among the orders: 0 best .. 1 worst
// (every order up to 8 sources, a sample of them above).
//
// A dependent source's list per binding is drawn shorter than a container (2 - 50 against
// 10 - 1000): the assumption the planner's dependent-first rule rests on. --long-lists draws
// both alike, to see what the rule costs when it is wrong.
//
//   model [--sets N] [--ks 3,4,...] [--seed S] [--long-lists]   the table, N sets per row
//   model --check                                                the cost model against real loops
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

#include "dreamor/index_set/plan.hpp"

namespace {

constexpr std::size_t kMaxSources = 16;
constexpr std::size_t kMaxPreds   = 136;  // a clique on 16 columns is 120, plus a filter per column
constexpr std::size_t kExactUpTo  = 8;    // rank over every order up to here, a sample above
constexpr std::size_t kSampled    = 4000;
bool g_long_lists                 = false;  // --long-lists: a dependent list as long as a container

struct Pred {
  std::uint64_t mask;
  double keeps;  // the share of what it sees that passes
};

// One set: source i binds column i; deps[i] are the columns it reads (0: a container).
struct Query {
  std::size_t k = 0;
  std::array<double, kMaxSources> size{};
  std::array<std::uint64_t, kMaxSources> deps{};
  std::vector<Pred> preds;
};

using Order = std::vector<std::size_t>;

// The planner's order, the real one: planVisitOrder over the masks makeSet would hand it.
template <std::size_t K>
Order plannedFixed(const Query& q) {
  std::array<std::uint64_t, K> produces{};
  std::array<std::uint64_t, K> depends{};
  std::array<std::uint64_t, kMaxPreds> preds{};  // unused ones are 0: no columns, no effect
  for (std::size_t i = 0; i < K; ++i) {
    produces[i] = std::uint64_t{1} << i;
    depends[i]  = q.deps[i];
  }
  for (std::size_t p = 0; p < q.preds.size(); ++p) preds[p] = q.preds[p].mask;
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

// T(S) for every subset S: from S without its lowest source, times that source's size and the
// selectivities of the predicates it completes.
std::vector<double> survivors(const Query& q) {
  std::vector<std::vector<const Pred*>> with(q.k);
  for (const Pred& p : q.preds)
    for (std::size_t i = 0; i < q.k; ++i)
      if ((p.mask >> i) & 1U) with[i].push_back(&p);
  std::vector<double> t(std::size_t{1} << q.k, 1.0);
  for (std::uint64_t s = 1; s < t.size(); ++s) {
    const auto low = static_cast<std::size_t>(std::countr_zero(s));
    double v       = t[s & (s - 1)] * q.size[low];
    for (const Pred* p : with[low])
      if ((p->mask & ~s) == 0) v *= p->keeps;
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

// The cheapest and the dearest order's cost, over subsets.
std::pair<double, double> bestAndWorst(const Query& q, const std::vector<double>& t) {
  constexpr double kInf = std::numeric_limits<double>::infinity();
  std::vector<double> lo(t.size(), kInf), hi(t.size(), -kInf);
  lo[0] = hi[0] = 0;
  for (std::uint64_t s = 1; s < t.size(); ++s) {
    for (std::size_t i = 0; i < q.k; ++i) {
      const std::uint64_t before = s & ~(std::uint64_t{1} << i);
      if (before == s || (q.deps[i] & ~before) != 0 || lo[before] == kInf) continue;
      const double step = t[before] * q.size[i];
      lo[s]             = std::min(lo[s], lo[before] + step);
      hi[s]             = std::max(hi[s], hi[before] + step);
    }
  }
  return {lo.back(), hi.back()};
}

// The costs of every order the dependencies allow (small k), or of a sample of them.
void everyOrder(
    const Query& q, const std::vector<double>& t, std::uint64_t set, double cost, std::vector<double>& out) {
  if (set == (std::uint64_t{1} << q.k) - 1) {
    out.push_back(cost);
    return;
  }
  for (std::size_t s = 0; s < q.k; ++s) {
    if (((set >> s) & 1U) != 0 || (q.deps[s] & ~set) != 0) continue;
    everyOrder(q, t, set | (std::uint64_t{1} << s), cost + t[set] * q.size[s], out);
  }
}
std::vector<double> orderCosts(const Query& q, const std::vector<double>& t, std::mt19937_64& g) {
  std::vector<double> out;
  if (q.k <= kExactUpTo) {
    everyOrder(q, t, 0, 0, out);
  } else {
    for (std::size_t n = 0; n < kSampled; ++n) {  // each step a source picked at random among those ready
      Order order;
      std::uint64_t set = 0;
      for (std::size_t step = 0; step < q.k; ++step) {
        std::vector<std::size_t> ready;
        for (std::size_t s = 0; s < q.k; ++s)
          if (((set >> s) & 1U) == 0 && (q.deps[s] & ~set) == 0) ready.push_back(s);
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

// Greedy on what it believes T to be: next, the source after which the fewest partial tuples
// survive.
Order bySize(const Query& q, const std::vector<double>& t) {
  Order order;
  std::uint64_t set = 0;
  for (std::size_t step = 0; step < q.k; ++step) {
    std::size_t best = q.k;
    for (std::size_t s = 0; s < q.k; ++s) {
      if (((set >> s) & 1U) != 0 || (q.deps[s] & ~set) != 0) continue;
      if (best == q.k || t[set | (std::uint64_t{1} << s)] < t[set | (std::uint64_t{1} << best)]) best = s;
    }
    order.push_back(best);
    set |= std::uint64_t{1} << best;
  }
  return order;
}

// What run time could know: the sizes, not the selectivities - each predicate taken to keep `guess`.
std::vector<double> survivorsGuessed(Query q, double guess) {
  for (Pred& p : q.preds) p.keeps = guess;
  return survivors(q);
}

// Planners that do not exist: plan.hpp's rules - most predicates run, then the smallest gap -
// with other ways to break the ties left. Without kTieDependent, declaration order alone (no
// rule 4); with kTieSize, then the smaller source (run time). The library's own is
// planned(): kTieDependent alone, which this reproduces (model --check compares the two).
enum TieRule : unsigned { kTieSize = 1, kTieDependent = 2 };

Order namesThen(const Query& q, unsigned rules) {
  // a < b when a comes first on the tie rules
  const auto before = [&](std::size_t a, std::size_t b) {
    if ((rules & kTieDependent) && (q.deps[a] != 0) != (q.deps[b] != 0)) return q.deps[a] != 0;
    if ((rules & kTieSize) && q.size[a] != q.size[b]) return q.size[a] < q.size[b];
    return false;  // declaration order: the one met first stays
  };
  Order order;
  std::uint64_t bound = 0;
  for (std::size_t step = 0; step < q.k; ++step) {
    std::size_t best = q.k, best_activated = 0, best_gap = 0;
    for (std::size_t s = 0; s < q.k; ++s) {
      if (((bound >> s) & 1U) != 0 || (q.deps[s] & ~bound) != 0) continue;
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
      if (gap == ~std::size_t{0}) gap = 0;
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

// --- making sets up --------------------------------------------------------------------------
// A shape is which pairs of columns a predicate reads; the columns are then shuffled, so that
// no shape lines up with declaration order (the planner's last tie-break).
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
  if (shape == "chain" || shape == "cycle")
    for (std::size_t i = 0; i + 1 < k; ++i) masks.push_back(pair(i, i + 1));
  if (shape == "cycle") masks.push_back(pair(k - 1, 0));
  if (shape == "star")
    for (std::size_t i = 1; i < k; ++i) masks.push_back(pair(0, i));
  if (shape == "clique")
    for (std::size_t i = 0; i < k; ++i)
      for (std::size_t j = i + 1; j < k; ++j) masks.push_back(pair(i, j));
  if (shape == "random") {
    const double p = std::min(0.4, 3.0 / static_cast<double>(k));  // about 1.5 predicates a column at any k
    for (std::size_t i = 0; i < k; ++i)
      for (std::size_t j = i + 1; j < k; ++j)
        if (std::bernoulli_distribution(p)(g)) masks.push_back(pair(i, j));
  }
  if (shape == "filters") {
    for (std::size_t i = 0; i < k; ++i)
      if (std::bernoulli_distribution(0.6)(g)) masks.push_back(std::uint64_t{1} << i);
    masks.push_back(pair(0, 1));  // one predicate across two columns, so not all is filtering
  }
  for (const std::uint64_t m : masks) q.preds.push_back({m, logUniform(g, 0.01, 1.0)});
  // A dependent source reads one column declared before it, as a machine's item list reads the
  // machine; a container's size is its rows, a dependent one's its list per binding.
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

// --- the table -------------------------------------------------------------------------------
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
          for (std::size_t c = 0; c < overall.size(); ++c) r[c].push_back(regrets[c]);
          // Ties count as half: the planner's order is one of the orders that cost what it costs.
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
        for (std::size_t c = 0; c < overall.size(); ++c)
          overall[c].all.insert(overall[c].all.end(), r[c].begin(), r[c].end());
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

// --- the model against real loops ------------------------------------------------------------
// Small sets with whole sizes, predicates that keep a hash's 1 in round(1/keeps), and nested
// loops that run each predicate as soon as its columns are bound, as makeSet does: the
// candidates they look at, against cost().
std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

std::uint64_t loops(const Query& q, const Order& order) {
  std::array<std::uint64_t, kMaxSources> value{};
  std::uint64_t looked                                  = 0;
  std::function<void(std::size_t, std::uint64_t)> visit = [&](std::size_t step, std::uint64_t set) {
    if (step == q.k) return;
    const std::size_t s = order[step];
    const auto n        = static_cast<std::uint64_t>(q.size[s]);
    for (std::uint64_t v = 0; v < n; ++v) {
      ++looked;
      value[s]                 = v;  // a dependent source's elements: its list's, whatever the binding
      const std::uint64_t next = set | (std::uint64_t{1} << s);
      bool passes              = true;
      for (std::size_t p = 0; p < q.preds.size() && passes; ++p) {
        const Pred& pr = q.preds[p];
        if ((pr.mask & ~next) != 0 || (pr.mask & ~set) == 0) continue;  // not yet, or ran already
        std::uint64_t h = p * 0x51afd7ed558ccd1ULL;
        for (std::size_t c = 0; c < q.k; ++c)
          if ((pr.mask >> c) & 1U) h = mix(h ^ (value[c] * 0x9e3779b97f4a7c15ULL + c));
        passes = h % static_cast<std::uint64_t>(std::llround(1 / pr.keeps)) == 0;
      }
      if (passes) visit(step + 1, next);
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
    for (std::size_t i = 0; i < k; ++i) q.size[i] = std::round(std::min(q.size[i], q.deps[i] ? 12.0 : 40.0));
    for (Pred& p : q.preds) p.keeps = 1 / std::round(1 / std::max(p.keeps, 0.05));
    const std::vector<double> t = survivors(q);
    replica_mismatch += planned(q) != namesThen(q, kTieDependent);  // this file's copy of the rules
    for (const auto& order : {planned(q), bySize(q, t)}) {
      ratio.push_back(static_cast<double>(loops(q, order)) / costOf(q, t, order));
    }
    std::vector<double> costs;  // the subset program against listing every order
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
      for (std::string item; std::getline(list, item, ',');) ks.push_back(std::stoul(item));
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
