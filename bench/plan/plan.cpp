// SPDX-License-Identifier: Apache-2.0
// File: bench/plan/plan.cpp
//
// How good is makeSet's visit order? plan.hpp picks it at compile time, greedily, from column
// names alone - no sizes, no selectivities: the source that lets the most predicates run next,
// then the one closest to a predicate, then declaration order. This generates one set of three
// columns (A, B, C) in every order it can be visited in and times each, so the planner's choice
// can be set against the best and worst of them.
//
// Variants (one per process run):
//   free     makeSet<A, B, C>(as, bs, cs).where(...): the order the planner picks
//   XYZ      the sources forced into order X, Y, Z (a permutation of ABC): the first is the
//            container, each next one a dependent source that hands back the whole container
//            but reads the column before it - only that order satisfies the dependencies. ABC is
//            what generation in declaration order would do.
//   late     one predicate over all three columns: it runs once all are bound, as filtering a
//            finished product would
//   SU, US   the join shape only: makeSet<>(S, U) and makeSet<>(U, S), S = (A, B), U = (B, C);
//            the set visited second is looked up by B
//
// Shapes - which predicates the set has (p over two columns, q over one):
//   pair      p(B, C)
//   filters   q(A), q(C)
//   chain     p(A, B), p(B, C)
//   triangle  p(A, B), p(B, C), p(A, C)
//   join      S = A x B where p(A, B), U = B x C where p(B, C), built untimed; the join is timed
// Each predicate keeps 1 in den of what it sees. PREDS random decides by a hash of the ids: a
// fixed, independent selectivity per predicate, which a branch predictor cannot guess.
// PREDS periodic keeps (x + y + salt) % den == 0: the same selectivity in a pattern it can. An
// order that is faster under random predicates only is faster at predicting branches, not at
// calling predicates less.
//
// Prints one CSV line: the planner's order, the rows, an order-free checksum (every variant of a
// case must agree), the time of collect(), and - in the counting build (-DPLAN_COUNT) - how many
// times predicates were called.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "dreamor/index_set.hpp"

struct E {
  std::uint32_t id;
};
using ACol = dreamor::Col<const E*, "a">;
using BCol = dreamor::Col<const E*, "b">;
using CCol = dreamor::Col<const E*, "c">;
using Ptrs = std::vector<const E*>;

static std::uint64_t g_evals = 0;  // predicate calls, counted in the PLAN_COUNT build only
static bool g_periodic       = false;

static inline std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
static inline bool keep(std::uint64_t den, std::uint64_t salt, std::uint32_t x, std::uint32_t y = 0) {
#ifdef PLAN_COUNT
  ++g_evals;
#endif
  return (g_periodic ? x + y + salt : mix(salt ^ (std::uint64_t{x} << 32) ^ y)) % den == 0;
}

// --- the predicates of each shape, as lambdas over the columns they read --------------------
struct Dens {
  std::uint64_t d1 = 1, d2 = 1, d3 = 1;
};
static auto pAB(std::uint64_t den) {
  return [den](ACol a, BCol b) { return keep(den, 0x1111, a->id, b->id); };
}
static auto pBC(std::uint64_t den) {
  return [den](BCol b, CCol c) { return keep(den, 0x2222, b->id, c->id); };
}
static auto pAC(std::uint64_t den) {
  return [den](ACol a, CCol c) { return keep(den, 0x3333, a->id, c->id); };
}
static auto qA(std::uint64_t den) {
  return [den](ACol a) { return keep(den, 0x4444, a->id); };
}
static auto qC(std::uint64_t den) {
  return [den](CCol c) { return keep(den, 0x5555, c->id); };
}

// The shape's predicates on a builder, each where it can run - or, late, folded into one over
// A, B and C, so that it runs only once all three are bound.
using Set = dreamor::SetOf<ACol, BCol, CCol>;
template <typename Builder>
static Set withPredicates(std::string_view shape, const Dens& d, Builder b, bool late) {
  if (shape == "pair") {
    if (late) return b.where([p = pBC(d.d1)](ACol, BCol bb, CCol c) { return p(bb, c); }).collect();
    return b.where(pBC(d.d1)).collect();
  }
  if (shape == "filters") {
    if (late) {
      return b.where([p = qA(d.d1), q = qC(d.d2)](ACol a, BCol, CCol c) { return p(a) && q(c); }).collect();
    }
    return b.where(qA(d.d1)).where(qC(d.d2)).collect();
  }
  if (shape == "chain") {
    if (late) {
      return b.where([p = pAB(d.d1), q = pBC(d.d2)](ACol a, BCol bb, CCol c) { return p(a, bb) && q(bb, c); })
          .collect();
    }
    return b.where(pAB(d.d1)).where(pBC(d.d2)).collect();
  }
  if (late) {
    return b
        .where([p = pAB(d.d1), q = pBC(d.d2), r = pAC(d.d3)](ACol a, BCol bb, CCol c) {
          return p(a, bb) && q(bb, c) && r(a, c);
        })
        .collect();
  }
  return b.where(pAB(d.d1)).where(pBC(d.d2)).where(pAC(d.d3)).collect();
}

template <typename Builder>
static std::string plannedOrder(std::string_view shape, const Dens& d, const Builder& b) {
  auto name = [](const auto& order) {
    std::string s;
    for (const std::size_t source : order) s += static_cast<char>('A' + source);
    return s;
  };
  if (shape == "pair") return name(decltype(b.where(pBC(d.d1)))::kVisitOrder);
  if (shape == "filters") return name(decltype(b.where(qA(d.d1)).where(qC(d.d2)))::kVisitOrder);
  if (shape == "chain") return name(decltype(b.where(pAB(d.d1)).where(pBC(d.d2)))::kVisitOrder);
  return name(decltype(b.where(pAB(d.d1)).where(pBC(d.d2)).where(pAC(d.d3)))::kVisitOrder);
}

// --- forcing an order ------------------------------------------------------------------------
template <std::size_t I>
using ColAt = std::tuple_element_t<I, std::tuple<ACol, BCol, CCol>>;

// Source I of makeSet<A, B, C>(...) when the visit order is to be X, Y, Z.
template <std::size_t I, std::size_t X, std::size_t Y, std::size_t Z>
static decltype(auto) sourceFor(const std::array<Ptrs, 3>& ptrs) {
  if constexpr (I == X) {
    return (ptrs[I]);  // the container itself
  } else if constexpr (I == Y) {
    return [&v = ptrs[I]](ColAt<X>) -> const Ptrs& { return v; };
  } else {
    return [&v = ptrs[I]](ColAt<Y>) -> const Ptrs& { return v; };
  }
}
template <std::size_t X, std::size_t Y, std::size_t Z>
static auto forced(const std::array<Ptrs, 3>& ptrs) {
  return dreamor::makeSet<ACol, BCol, CCol>(
      sourceFor<0, X, Y, Z>(ptrs), sourceFor<1, X, Y, Z>(ptrs), sourceFor<2, X, Y, Z>(ptrs));
}

template <std::size_t X, std::size_t Y, std::size_t Z>
static Set runForced(std::string_view shape, const Dens& d, const std::array<Ptrs, 3>& ptrs) {
  return withPredicates(shape, d, forced<X, Y, Z>(ptrs), false);
}
using RunForced          = Set (*)(std::string_view, const Dens&, const std::array<Ptrs, 3>&);
constexpr std::size_t kA = 0, kB = 1, kC = 2;
constexpr std::pair<std::string_view, RunForced> kOrders[] = {
    {"ABC", runForced<kA, kB, kC>},
    {"ACB", runForced<kA, kC, kB>},
    {"BAC", runForced<kB, kA, kC>},
    {"BCA", runForced<kB, kC, kA>},
    {"CAB", runForced<kC, kA, kB>},
    {"CBA", runForced<kC, kB, kA>},
};

template <typename Tup>
static std::uint64_t checksum(const dreamor::IndexSet<Tup>& set) {
  std::uint64_t sum = 0;
  for (const auto& row : set) {
    const auto& a = std::get<ACol>(row);
    const auto& b = std::get<BCol>(row);
    const auto& c = std::get<CCol>(row);
    sum += mix((std::uint64_t{a->id} << 42) ^ (std::uint64_t{b->id} << 21) ^ c->id);
  }
  return sum + set.size() * 0x9e3779b97f4a7c15ULL;
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--columns") {
    std::puts("shape,nA,nB,nC,d1,d2,d3,preds,variant,planned,rows,checksum,ms,evals");
    return 0;
  }
  if (argc != 10 || (std::string_view{argv[8]} != "random" && std::string_view{argv[8]} != "periodic")) {
    std::fprintf(stderr, "usage: plan SHAPE nA nB nC d1 d2 d3 random|periodic free|late|XYZ|SU|US\n");
    return 2;
  }
  const std::string_view shape = argv[1], variant = argv[9];
  g_periodic = std::string_view{argv[8]} == "periodic";
  const std::array<std::size_t, 3> n{std::stoul(argv[2]), std::stoul(argv[3]), std::stoul(argv[4])};
  const Dens d{std::stoull(argv[5]), std::stoull(argv[6]), std::stoull(argv[7])};

  std::array<std::vector<E>, 3> entities;
  std::array<Ptrs, 3> ptrs;
  for (std::size_t s = 0; s < 3; ++s) {
    entities[s].resize(n[s]);
    for (std::uint32_t i = 0; i < n[s]; ++i) entities[s][i].id = i;
    for (const E& e : entities[s]) ptrs[s].push_back(&e);
  }

  // Times build() alone; the checksum reads the columns by type, so a set whose columns come in
  // another order sums the same.
  using Clk           = std::chrono::steady_clock;
  std::string planned = "-";
  double ms           = 0;
  std::size_t rows    = 0;
  std::uint64_t sum   = 0;
  bool ran            = false;
  const auto time     = [&](auto&& build) {
    g_evals        = 0;
    const auto t   = Clk::now();
    const auto out = build();
    ms             = std::chrono::duration<double, std::milli>(Clk::now() - t).count();
    rows           = out.size();
    sum            = checksum(out);
    ran            = true;
  };

  if (shape == "join") {
    const auto S = dreamor::makeSet<ACol, BCol>(ptrs[0], ptrs[1]).where(pAB(d.d1)).collect();
    const auto U = dreamor::makeSet<BCol, CCol>(ptrs[1], ptrs[2]).where(pBC(d.d2)).collect();
    if (variant == "SU") time([&] { return dreamor::makeSet<>(S, U).collect(); });
    if (variant == "US") time([&] { return dreamor::makeSet<>(U, S).collect(); });
    planned = variant;  // a join is visited in declaration order
  } else {
    const auto free_builder = dreamor::makeSet<ACol, BCol, CCol>(ptrs[0], ptrs[1], ptrs[2]);
    planned                 = plannedOrder(shape, d, free_builder);
    if (variant == "free" || variant == "late") {
      time([&] { return withPredicates(shape, d, free_builder, variant == "late"); });
    }
    for (const auto& [name, run] : kOrders) {
      if (variant == name) time([&] { return run(shape, d, ptrs); });
    }
  }
  if (!ran) {
    std::fprintf(stderr, "no variant %s for shape %s\n", argv[9], argv[1]);
    return 2;
  }

  std::printf("%s,%zu,%zu,%zu,%llu,%llu,%llu,%s,%s,%s,%zu,%llu,%.3f,%llu\n",
              argv[1],
              n[0],
              n[1],
              n[2],
              static_cast<unsigned long long>(d.d1),
              static_cast<unsigned long long>(d.d2),
              static_cast<unsigned long long>(d.d3),
              argv[8],
              argv[9],
              planned.c_str(),
              rows,
              static_cast<unsigned long long>(sum),
              ms,
              static_cast<unsigned long long>(g_evals));
}
