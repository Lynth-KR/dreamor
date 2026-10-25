// SPDX-License-Identifier: Apache-2.0
// File: bench/solver/probe.cpp
//
// Not part of the benchmarks. The catalog (bench/catalog) runs against a stub on purpose: a real
// solver is a black box, and how long it takes to make a variable moves with its insides and
// the machine, not with the containers in front of it. This probe looks at that black box
// afterwards, for one question at a time:
//   - how much of a real model build the containers are (this build against the stub, against
//     the real solver);
//   - whether a gap between the arms under the real solver is the containers' at all.
//
// It builds only the variables of one cell - K1 x K2 x K3, every route kept, ids in loop order -
// F families, both arms, in one fresh process per run:
//   dreamor  makeSet(...).collect(), then one VarTable per family
//   flat     rows as ids, one std::vector of variables per family, an unordered_map index
//
// Bounds (--bounds):
//   unique  every variable its own upper bound - its number among all the variables, from 1: the
//           worst case for a solver that shares variable domains by value - Concert keeps one
//           per distinct (lb, ub) in a hash table, which then holds one per variable
//   few     16 distinct upper bounds, as capacities or big-Ms tend to be
// PAD (0, 32, 128 or 512) adds that many register-only instructions to every bound computation,
// in both arms: a result that moves with it measures how the solver's memory accesses overlap
// from one variable to the next, not the containers.
//
// Built three ways by build.sh: against real Concert, against the benchmarks' Concert stub
// (bench/common/stub - the containers alone), and against OR-Tools MathOpt. Prints one CSV line.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sys/resource.h>

#include "dreamor/index_set.hpp"
#ifdef PROBE_MATHOPT
#  include "dreamor/mathopt.hpp"
#else
#  include "dreamor/concert.hpp"
#endif

struct E {
  std::uint32_t serial;
};
inline std::uint32_t keyName(const E* e) {
  return e->serial;
}
using K1Col = dreamor::Col<const E*, "k1">;
using K2Col = dreamor::Col<const E*, "k2">;
using K3Col = dreamor::Col<const E*, "k3">;

static bool g_unique     = true;
static std::size_t g_pad = 0;
static std::size_t g_n2 = 0, g_n3 = 0, g_families = 0;

static inline std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}

template <std::size_t K>
static inline void pad(std::uint64_t& x) {
  if constexpr (K > 0) {
    asm volatile("add $1, %0" : "+r"(x));
    pad<K - 1>(x);
  }
}

static bool padAllowed(std::size_t k) {
  return k == 0 || k == 32 || k == 128 || k == 512;
}

// The upper bound of one variable: its number among all the variables, or one of 16 values.
static double boundOf(std::uint32_t s1, std::uint32_t s2, std::uint32_t s3, std::size_t family) {
  std::uint64_t x = 0;
  switch (g_pad) {
    case 32: pad<32>(x); break;
    case 128: pad<128>(x); break;
    case 512: pad<512>(x); break;
    default: break;  // 0; main() turns away the rest
  }
  const std::uint64_t row = (std::uint64_t{s1} * g_n2 + s2) * g_n3 + s3;
  const std::uint64_t ub  = g_unique ? 1 + row * g_families + family : 1 + mix(row * g_families + family) % 16;
  return static_cast<double>(ub + (x & 0));
}

using Clk = std::chrono::steady_clock;
static double userMs() {
  rusage u;
  getrusage(RUSAGE_SELF, &u);
  return static_cast<double>(u.ru_utime.tv_sec) * 1e3 + static_cast<double>(u.ru_utime.tv_usec) / 1e3;
}
struct Clock {
  Clk::time_point wall = Clk::now();
  double user          = userMs();
  double wallMs() const { return std::chrono::duration<double, std::milli>(Clk::now() - wall).count(); }
};

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--columns") {
    std::puts("backend,arm,bounds,pad,n,families,keys_ms,family_ms,index_ms,total_ms,total_user_ms");
    return 0;
  }
  if (argc != 6) {
    std::fprintf(stderr, "usage: probe dreamor|flat N FAMILIES unique|few 0|32|128|512\n");
    return 2;
  }
  const std::string_view arm = argv[1];
  const std::size_t n_target = std::stoul(argv[2]), families = std::stoul(argv[3]);
  const std::string_view bounds = argv[4];
  g_unique                      = bounds == "unique";
  g_pad                         = std::stoul(argv[5]);
  if ((arm != "dreamor" && arm != "flat") || (bounds != "unique" && bounds != "few") || !padAllowed(g_pad)) {
    std::fprintf(stderr, "usage: probe dreamor|flat N FAMILIES unique|few 0|32|128|512\n");
    return 2;
  }
  const std::size_t n2 = 50, n3 = 100, n1 = std::max<std::size_t>(1, n_target / (n2 * n3));
  g_n2 = n2, g_n3 = n3, g_families = families;

  std::vector<E> e1(n1), e2(n2), e3(n3);
  std::vector<const E*> k1s, k2s, k3s;
  for (std::uint32_t i = 0; i < n1; ++i) k1s.push_back(&(e1[i] = E{i}));
  for (std::uint32_t i = 0; i < n2; ++i) k2s.push_back(&(e2[i] = E{i}));
  for (std::uint32_t i = 0; i < n3; ++i) k3s.push_back(&(e3[i] = E{i}));

#if defined(PROBE_MATHOPT)
  const char* backend = "mathopt";
  operations_research::math_opt::Model model;
  using Var     = operations_research::math_opt::Variable;
  using Table   = dreamor::mathopt::VarTable<K1Col, K2Col, K3Col>;
  auto newVar   = [&](double ub) { return model.AddVariable(0, ub, false, ""); };
  auto newTable = [&](const auto& set, const auto& ub) { return Table(model, set, 0, ub); };
#else
#  if defined(PROBE_STUB)
  const char* backend = "stub";
#  else
  const char* backend = "concert";
#  endif
  IloEnv env;
  using Var     = IloNumVar;
  using Table   = dreamor::concert::VarTable<K1Col, K2Col, K3Col>;
  auto newVar   = [&](double ub) { return IloNumVar(env, 0, ub, ILOFLOAT); };
  auto newTable = [&](const auto& set, const auto& ub) { return Table(env, set, 0, ub, ILOFLOAT); };
#endif

  double keys_ms = 0, index_ms = 0;
  std::vector<double> family_ms;
  const Clock total;
  if (arm == "dreamor") {
    Clock c;
    const auto set = dreamor::makeSet<K1Col, K2Col, K3Col>(k1s, k2s, k3s).collect();
    keys_ms        = c.wallMs();
    std::vector<Table> tables;
    tables.reserve(families);
    for (std::size_t f = 0; f < families; ++f) {
      const Clock cf;
      const auto ub = [f](K1Col a, K2Col b, K3Col c3) { return boundOf(a->serial, b->serial, c3->serial, f); };
      tables.push_back(newTable(set, ub));  // the first builds the set's row index
      family_ms.push_back(cf.wallMs());
    }
  } else {
    struct Row {
      std::uint32_t i1, i2, i3;
    };
    Clock c;
    std::vector<Row> rows;
    for (const E* a : k1s)
      for (const E* b : k2s)
        for (const E* c3 : k3s) rows.push_back({a->serial, b->serial, c3->serial});
    keys_ms = c.wallMs();
    std::vector<std::vector<Var>> vars(families);
    for (std::size_t f = 0; f < families; ++f) {
      const Clock cf;
      vars[f].reserve(rows.size());
      for (const Row& r : rows) vars[f].push_back(newVar(boundOf(r.i1, r.i2, r.i3, f)));
      family_ms.push_back(cf.wallMs());
    }
    const Clock ci;
    std::unordered_map<std::uint64_t, std::uint32_t> index;
    index.reserve(rows.size());
    for (std::uint32_t i = 0; i < rows.size(); ++i)
      index.emplace((std::uint64_t{rows[i].i1} << 42) | (std::uint64_t{rows[i].i2} << 21) | rows[i].i3, i);
    index_ms = ci.wallMs();
  }
  const double total_ms = total.wallMs(), total_user = userMs() - total.user;

  std::string fams;
  for (const double ms : family_ms) fams += (fams.empty() ? "" : ";") + std::to_string(ms);
  std::printf("%s,%s,%s,%zu,%zu,%zu,%.3f,%s,%.3f,%.3f,%.3f\n",
              backend,
              argv[1],
              argv[4],
              g_pad,
              n1 * n2 * n3,
              families,
              keys_ms,
              fams.c_str(),
              index_ms,
              total_ms,
              total_user);
}
