// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The timed visit-order experiment: one set of three columns, built in each order in which it can be visited.
//
// plan.hpp picks a set's visit order at compile time, greedily, from the names of the columns alone. It knows no sizes
// and no selectivities. It prefers the source that lets the most predicates run next, then the source that is fewest
// columns away from a predicate, then a dependent source over a container, then the source declared first. This
// program builds one set of the columns A, B and C in one visit order and times it, so that run.py can set the
// planner's choice against the best and the worst order.
//
// Each process run builds one variant:
//   free      of<cA, cB, cC>(in<cA>(as), in<cB>(bs), in<cC>(cs)).where(...), in the order that the planner picks.
//   XYZ       The sources in the order X, Y, Z, a permutation of ABC. X binds its column from its container. Y and Z
//             each bind their column from a callable that reads the column before it and returns the whole
//             container, so no other order meets the dependencies. ABC is the order in which the sources are
//             declared.
//   late      One predicate over all three columns. It runs only when all three columns are bound, as a filter over
//             the finished product does.
//   SU, US    The join shape only: of<cA, cB, cC>(in<cA, cB>(sS), in<cB, cC>(sU)) and
//             of<cB, cC, cA>(in<cB, cC>(sU), in<cA, cB>(sS)), where sS holds (A, B) and sU holds (B, C). The set
//             visited second is looked up by B.
//
// A shape is the predicates that the set has, where p reads two columns and q reads one:
//   pair      p(B, C)
//   filters   q(A), q(C)
//   chain     p(A, B), p(B, C)
//   triangle  p(A, B), p(B, C), p(A, C)
//   join      sS = A x B where p(A, B), and sU = B x C where p(B, C). Both sets are built before the timer starts,
//             and only the join is timed.
// Each predicate keeps 1 in den of the rows that it sees. If the predicates are random, a predicate decides by a hash
// of the ids, so that each predicate has a fixed, independent selectivity that a branch predictor cannot learn. If the
// predicates are periodic, a predicate keeps (x + y + salt) % den == 0: the same selectivity, in a pattern that a
// branch predictor can learn. If an order is faster under random predicates only, the order is better at predicting
// branches, not at calling the predicates less often.
//
// The program prints one CSV line: the planner's order, the number of rows, an order-free checksum, the time of the
// build, and the number of predicate calls. For the join shape, the variant stands in for the planner's order. Every
// variant of a case must print the same checksum. Only the counting build (-DPLAN_COUNT) counts the predicate calls,
// and the timed build prints 0 for them.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/index_set.hpp"

struct E {
  std::uint32_t id;

  [[nodiscard]] std::uint32_t primaryKey() const { return id; }
};

DREAMOR_ENTITY_COLUMN(cA, E);
DREAMOR_ENTITY_COLUMN(cB, E);
DREAMOR_ENTITY_COLUMN(cC, E);
using Refs = std::vector<dreamor::Ref<E>>;

// The number of predicate calls. Only the PLAN_COUNT build counts them.
static std::uint64_t g_evals = 0;
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

// The den of each predicate of a shape, in the order in which the shape lists its predicates.
struct Dens {
  std::uint64_t d1 = 1, d2 = 1, d3 = 1;
};

// The predicates of the shapes. Each function returns a column callable that reads the columns of its predicate.
static auto pAB(std::uint64_t den) {
  return [den](cA a, cB b) { return keep(den, 0x1111, a->id, b->id); };
}

static auto pBC(std::uint64_t den) {
  return [den](cB b, cC c) { return keep(den, 0x2222, b->id, c->id); };
}

static auto pAC(std::uint64_t den) {
  return [den](cA a, cC c) { return keep(den, 0x3333, a->id, c->id); };
}

static auto qA(std::uint64_t den) {
  return [den](cA a) { return keep(den, 0x4444, a->id); };
}

static auto qC(std::uint64_t den) {
  return [den](cC c) { return keep(den, 0x5555, c->id); };
}

// Adds the shape's predicates to set and builds it. Each predicate runs as soon as its columns are bound. If late is
// true, the predicates are folded into one over A, B and C, which runs only when all three columns are bound.
template <typename Set>
static auto withPredicates(std::string_view shape, const Dens& d, Set set, bool late) {
  if (shape == "pair") {
    if (late) {
      const auto pair_late = [p = pBC(d.d1)](cA, cB b, cC c) { return p(b, c); };
      return set.where(pair_late).build();
    }
    return set.where(pBC(d.d1)).build();
  }
  if (shape == "filters") {
    if (late) {
      const auto filters_late = [p = qA(d.d1), q = qC(d.d2)](cA a, cB, cC c) { return p(a) && q(c); };
      return set.where(filters_late).build();
    }
    return set.where(qA(d.d1)).where(qC(d.d2)).build();
  }
  if (shape == "chain") {
    if (late) {
      const auto chain_late = [p = pAB(d.d1), q = pBC(d.d2)](cA a, cB b, cC c) { return p(a, b) && q(b, c); };
      return set.where(chain_late).build();
    }
    return set.where(pAB(d.d1)).where(pBC(d.d2)).build();
  }
  if (late) {
    const auto triangle_late = [p = pAB(d.d1), q = pBC(d.d2), r = pAC(d.d3)](cA a, cB b, cC c) {
      return p(a, b) && q(b, c) && r(a, c);
    };
    return set.where(triangle_late).build();
  }
  return set.where(pAB(d.d1)).where(pBC(d.d2)).where(pAC(d.d3)).build();
}

template <typename Set>
static std::string plannedOrder(std::string_view shape, const Dens& d, const Set& set) {
  // Each source binds one column, so the letter after each 'c' names a source: "cA, cC, cB" becomes "ACB".
  auto name = [](std::string_view order) {
    std::string s;
    for (std::size_t i = 0; i + 1 < order.size(); ++i) {
      if (order[i] == 'c') { s += order[i + 1]; }
    }
    return s;
  };
  if (shape == "pair") { return name(decltype(set.where(pBC(d.d1)))::visitOrder()); }
  if (shape == "filters") { return name(decltype(set.where(qA(d.d1)).where(qC(d.d2)))::visitOrder()); }
  if (shape == "chain") { return name(decltype(set.where(pAB(d.d1)).where(pBC(d.d2)))::visitOrder()); }
  return name(decltype(set.where(pAB(d.d1)).where(pBC(d.d2)).where(pAC(d.d3)))::visitOrder());
}

template <std::size_t I>
using ColAt = std::tuple_element_t<I, std::tuple<cA, cB, cC>>;

// Returns the set whose visit order is X, Y, Z. X binds its column from its container. Y and Z each bind their column
// from a callable that reads the column before it, so neither source can be visited earlier. The head lists the
// columns in the visit order, and the checksum reads them by type.
template <std::size_t X, std::size_t Y, std::size_t Z>
static auto forced(const std::array<Refs, 3>& refs) {
  using namespace dreamor::index_set::set_builder;
  const auto y_after_x = [&v = refs[Y]](ColAt<X>) -> const Refs& { return v; };
  const auto z_after_y = [&v = refs[Z]](ColAt<Y>) -> const Refs& { return v; };
  return of<ColAt<X>, ColAt<Y>, ColAt<Z>>(in<ColAt<X>>(refs[X]), in<ColAt<Y>>(y_after_x), in<ColAt<Z>>(z_after_y));
}

// Times the forced order X, Y, Z if it is the variant asked for.
template <std::size_t X, std::size_t Y, std::size_t Z, typename Time>
static void runForced(std::string_view variant,
                      std::string_view shape,
                      const Dens& d,
                      const std::array<Refs, 3>& refs,
                      const Time& time) {
  constexpr std::array<char, 3> kName{
      static_cast<char>('A' + X), static_cast<char>('A' + Y), static_cast<char>('A' + Z)};
  // The dependencies leave one order, and no predicate can change it.
  constexpr std::array<char, 10> kVisit{'c', kName[0], ',', ' ', 'c', kName[1], ',', ' ', 'c', kName[2]};
  static_assert(decltype(forced<X, Y, Z>(std::declval<const std::array<Refs, 3>&>()))::visitOrder() ==
                std::string_view{kVisit.data(), kVisit.size()});
  if (variant == std::string_view{kName.data(), kName.size()}) {
    time([&] { return withPredicates(shape, d, forced<X, Y, Z>(refs), false); });
  }
}

constexpr std::size_t kA = 0, kB = 1, kC = 2;

template <typename... Columns>
static std::uint64_t checksum(const dreamor::IndexSet<Columns...>& set) {
  std::uint64_t sum = 0;
  for (const auto& row : set) {
    const auto& a = std::get<cA>(row);
    const auto& b = std::get<cB>(row);
    const auto& c = std::get<cC>(row);
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

  std::array<dreamor::EntityVector<E>, 3> entities;
  std::array<Refs, 3> refs;
  for (std::size_t s = 0; s < 3; ++s) {
    entities[s].reserve(n[s]);
    for (std::uint32_t i = 0; i < n[s]; ++i) {
      static_cast<void>(entities[s].emplace(E{i}));
    }
    entities[s].freeze();
    for (std::uint32_t i = 0; i < n[s]; ++i) {
      refs[s].push_back(entities[s].at(i));
    }
  }

  // The timer covers the lambda that writes and builds the set, and it stops before the checksum. The predicate count
  // starts from 0 there too, so it leaves out the predicates that build the join's two sets. The checksum reads the
  // columns by type, so a set whose columns come in another order has the same checksum.
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

  using namespace dreamor::index_set::set_builder;
  if (shape == "join") {
    const auto sS = of<cA, cB>(in<cA>(refs[0]), in<cB>(refs[1])).where(pAB(d.d1)).build();
    const auto sU = of<cB, cC>(in<cB>(refs[1]), in<cC>(refs[2])).where(pBC(d.d2)).build();
    if (variant == "SU") {
      time([&] { return of<cA, cB, cC>(in<cA, cB>(sS), in<cB, cC>(sU)).build(); });
    }
    if (variant == "US") {
      time([&] { return of<cB, cC, cA>(in<cB, cC>(sU), in<cA, cB>(sS)).build(); });
    }
    // A join is visited in the order in which its generators are written.
    planned = variant;
  } else {
    const auto freeOrder = of<cA, cB, cC>(in<cA>(refs[0]), in<cB>(refs[1]), in<cC>(refs[2]));
    planned              = plannedOrder(shape, d, freeOrder);
    if (variant == "free" || variant == "late") {
      time([&] { return withPredicates(shape, d, freeOrder, variant == "late"); });
    }
    runForced<kA, kB, kC>(variant, shape, d, refs, time);
    runForced<kA, kC, kB>(variant, shape, d, refs, time);
    runForced<kB, kA, kC>(variant, shape, d, refs, time);
    runForced<kB, kC, kA>(variant, shape, d, refs, time);
    runForced<kC, kA, kB>(variant, shape, d, refs, time);
    runForced<kC, kB, kA>(variant, shape, d, refs, time);
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
