// SPDX-License-Identifier: Apache-2.0
// bench.cpp - measures one implementation (arm) in one cell of the design, one repetition, in
// one process, and prints one CSV line (columns: BENCH_COLUMNS in run_doe.py).
//
// Model: a 3-index set (K1, K2, K3). Each K1 has a list of K2s, C2(k1), and a list of K3s,
// C3(k1); the rows are { (k1, k2, k3) : k2 in C2(k1), k3 in C3(k1), keep(k1, k2, k3) }.
// Every row gets one variable. The work is what model code does with an indexed variable
// family: build it, group it by a key, sum each group, iterate it, look keys up.
//
// Factors (two levels each):
//   W  workload    prefix : group by K1 only            | mixed : group by K1, by K2 and by K3
//   O  K2 owner    shared : all K1s draw K2s from a pool | owned : each K2 belongs to one K1
//   D  density     dense  : every candidate is a row     | sparse: 10% of candidates (predicate)
//   S  fan-out     uniform: same list sizes per K1       | skewed: same mean, Pareto-distributed
//
// Arms - each does the same work the way that style of code naturally does it:
//   dreamor : makeSet(...).where(keep).collect(), one VarTable, groupBy per grouping key,
//             group sums with sumOf(env, rows, vX).build().
//   map     : the usual hand-written model code (as in examples/production_planning_plain.hpp):
//             std::map<tuple<E1*, E2*, E3*>, IloNumVar>, filled by nested loops over the
//             data. Group sums loop over the data again and look each variable up; the K2
//             and K3 groupings first build owner lists (which K1s use this K2 / K3), so no
//             grouping scans the whole data.
//   flat    : hand-optimized: rows and variables in parallel vectors, groupings as CSR
//             (offsets + row indices by counting sort), point lookups through a
//             std::unordered_map from a packed integer key to the row.
//
// The solver side is the same in every arm: the stub in bench/stub makes each variable a
// 16-byte heap object and each expression a vector of variable pointers, so it adds the
// same work to every arm and drops out of the comparison.
//
// Measurements:
//   build_ms     rows + container + variables
//   prep_ms      groupings (dreamor: groupBy; map: owner lists; flat: CSR)
//   g1/g2/g3_ms  one expression per group, by K1 / K2 / K3 (g2, g3 only when W = mixed)
//   traverse_ms  every variable once
//   hit_ns       one lookup of an existing key; miss_ns: one membership test of an absent key
//   container_bytes  resident-memory growth over build + prep, minus the variables' own heap
//   peak_rss_bytes   process peak
//   groups, check_groups, check_traverse: the same in every arm iff the arms hand out the same
//                variables in the same groups (see consume()).
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <sys/resource.h>
#include <unistd.h>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/logger/logger_light.hpp"

// ---------------------------------------------------------------------------------------------
struct E1 {
  std::uint32_t id;
};
struct E2 {
  std::uint32_t id;
};
struct E3 {
  std::uint32_t id;
};
inline std::uint32_t keyName(const E1* e) {
  return e->id;
}
inline std::uint32_t keyName(const E2* e) {
  return e->id;
}
inline std::uint32_t keyName(const E3* e) {
  return e->id;
}

using K1Col = dreamor::Col<const E1*, "k1">;
using K2Col = dreamor::Col<const E2*, "k2">;
using K3Col = dreamor::Col<const E3*, "k3">;

// ---------------------------------------------------------------------------------------------
struct Args {
  std::string impl, W = "prefix", O = "shared", D = "dense", S = "uniform";
  std::size_t n_target = 1000000;
  std::uint32_t f2 = 50, f3 = 100;  // mean K2s / K3s per K1
  std::uint32_t rep   = 0;
  std::uint64_t seed  = 1;
  std::size_t lookups = 1000000;
};

static Args parse(int argc, char** argv) {
  Args a;
  const auto argCount = static_cast<std::size_t>(argc);
  for (std::size_t i = 1; i + 1 < argCount; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--impl")
      a.impl = v;
    else if (k == "--W")
      a.W = v;
    else if (k == "--O")
      a.O = v;
    else if (k == "--D")
      a.D = v;
    else if (k == "--S")
      a.S = v;
    else if (k == "--N")
      a.n_target = std::strtoull(v.c_str(), nullptr, 10);
    else if (k == "--f2")
      a.f2 = static_cast<std::uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    else if (k == "--f3")
      a.f3 = static_cast<std::uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    else if (k == "--rep")
      a.rep = static_cast<std::uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
    else if (k == "--seed")
      a.seed = std::strtoull(v.c_str(), nullptr, 10);
    else if (k == "--lookups")
      a.lookups = std::strtoull(v.c_str(), nullptr, 10);
    else {
      DREAMOR_LOG_ERROR("unknown option {}", k);
      std::exit(2);
    }
  }
  if (a.impl != "dreamor" && a.impl != "map" && a.impl != "flat") {
    DREAMOR_LOG_ERROR("--impl dreamor|map|flat");
    std::exit(2);
  }
  return a;
}

// ---------------------------------------------------------------------------------------------
using Clk = std::chrono::steady_clock;
template <typename F>
static double timeMs(F&& f) {
  const auto t = Clk::now();
  f();
  return std::chrono::duration<double, std::milli>(Clk::now() - t).count();
}

// Signed: the callers subtract one reading from another.
static std::int64_t residentBytes() {
  std::int64_t pages = 0, resident = 0;
  if (FILE* f = std::fopen("/proc/self/statm", "r")) {
    if (std::fscanf(f, "%" SCNd64 " %" SCNd64, &pages, &resident) != 2) resident = 0;
    std::fclose(f);
  }
  return resident * static_cast<std::int64_t>(sysconf(_SC_PAGESIZE));
}
static std::int64_t peakBytes() {
  struct rusage u;
  getrusage(RUSAGE_SELF, &u);
  return static_cast<std::int64_t>(u.ru_maxrss) * 1024;
}

static inline std::uint64_t mix(std::uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
static inline std::uint64_t key3(const E1* a, const E2* b, const E3* c) {
  return (std::uint64_t{a->id} << 42) ^ (std::uint64_t{b->id} << 21) ^ c->id;
}

// ---------------------------------------------------------------------------------------------
// The model data. Rows are added K1 by K1 until there are n_target of them, so every cell has
// the same row count whatever its density and fan-out.
struct Model {
  std::vector<std::unique_ptr<E1>> e1;
  std::vector<std::unique_ptr<E2>> e2;
  std::vector<std::unique_ptr<E3>> e3;  // the last one is in no C3 list: the absent key
  std::vector<const E1*> k1s;
  std::vector<std::vector<const E2*>> c2;  // c2[k1->id]
  std::vector<std::vector<const E3*>> c3;  // c3[k1->id]
  const E3* absent3 = nullptr;
  bool sparse       = false;
  std::size_t rows  = 0;

  bool keep(const E1* a, const E2* b, const E3* c) const { return !sparse || mix(key3(a, b, c)) % 10 == 0; }
};

static Model makeModel(const Args& a) {
  Model m;
  m.sparse = a.D == "sparse";
  std::mt19937_64 g(a.seed * 7919 + 17);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  const bool skewed = a.S == "skewed";
  const bool owned  = a.O == "owned";
  // Pareto (alpha = 1.5) weights with mean 1: a few K1s get large lists.
  auto weight = [&]() {
    if (!skewed) return 1.0;
    const double alpha = 1.5, xm = (alpha - 1.0) / alpha;
    return std::min(xm / std::pow(1.0 - u(g), 1.0 / alpha), 50.0);
  };
  const std::size_t pool3 = std::size_t{a.f3} * 4;  // K3s are drawn from one shared pool
  for (std::size_t i = 0; i <= pool3; ++i) {
    m.e3.push_back(std::make_unique<E3>(E3{static_cast<std::uint32_t>(i)}));
  }
  m.absent3 = m.e3.back().get();
  std::vector<const E2*> shared2;
  if (!owned) {
    const std::size_t pool2 = std::size_t{a.f2} * 50;
    for (std::size_t i = 0; i < pool2; ++i) {
      m.e2.push_back(std::make_unique<E2>(E2{static_cast<std::uint32_t>(i)}));
      shared2.push_back(m.e2.back().get());
    }
  }
  std::size_t rows = 0;
  while (rows < a.n_target) {
    const auto id = static_cast<std::uint32_t>(m.k1s.size());
    m.e1.push_back(std::make_unique<E1>(E1{id}));
    const E1* k1 = m.e1.back().get();
    m.k1s.push_back(k1);
    std::size_t n2 = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(a.f2 * weight())));
    if (!owned) n2 = std::min(n2, shared2.size());
    const std::size_t n3 =
        std::max<std::size_t>(1, std::min(pool3, static_cast<std::size_t>(std::lround(a.f3 * weight()))));
    std::vector<const E2*> l2;
    if (owned) {
      for (std::size_t j = 0; j < n2; ++j) {
        m.e2.push_back(std::make_unique<E2>(E2{static_cast<std::uint32_t>(m.e2.size())}));
        l2.push_back(m.e2.back().get());
      }
    } else {
      const std::size_t start = (std::size_t{id} * 7) % shared2.size();
      for (std::size_t j = 0; j < n2; ++j) l2.push_back(shared2[(start + j) % shared2.size()]);
    }
    std::vector<const E3*> l3;
    const std::size_t start3 = mix(id) % pool3;
    for (std::size_t j = 0; j < n3; ++j) l3.push_back(m.e3[(start3 + j) % pool3].get());
    for (const E2* b : l2)
      for (const E3* c : l3) rows += m.keep(k1, b, c);
    m.c2.push_back(std::move(l2));
    m.c3.push_back(std::move(l3));
  }
  m.rows = rows;
  return m;
}

// Each variable carries a tag fixed by its row's key, as its upper bound. The arms are then
// checked against each other by the tags they hand out, independently of addresses.
static inline double tagOf(const E1* a, const E2* b, const E3* c) {
  return static_cast<double>(mix(key3(a, b, c)) % 1000003);
}

// Heap bytes one stub variable takes (malloc overhead included), measured at start-up and
// subtracted from container_bytes. The probe is not freed: freed blocks would be reused by
// the variables and the subtraction would overshoot.
static double heapBytesPerVar() {
  const std::size_t n = 1000000;
  std::vector<IloNumVarI*> probe;
  probe.reserve(n);
  const std::int64_t before = residentBytes();
  for (std::size_t i = 0; i < n; ++i) probe.push_back(new IloNumVarI{0, 1});
  return static_cast<double>(residentBytes() - before) / n;
}

struct Sample {
  std::vector<std::tuple<const E1*, const E2*, const E3*>> hit, miss;
};

static Sample makeSample(const Model& m, const Args& a) {
  Sample s;
  const std::size_t n = std::min(a.lookups, m.rows);
  std::mt19937_64 g(a.seed * 104729 + 3);
  s.hit.reserve(n);
  s.miss.reserve(n);
  while (s.hit.size() < n) {
    const E1* k1   = m.k1s[g() % m.k1s.size()];
    const auto& l2 = m.c2[k1->id];
    const auto& l3 = m.c3[k1->id];
    const E2* b    = l2[g() % l2.size()];
    const E3* c    = l3[g() % l3.size()];
    if (!m.keep(k1, b, c)) continue;
    s.hit.emplace_back(k1, b, c);
    s.miss.emplace_back(k1, b, m.absent3);
  }
  return s;
}

// ---------------------------------------------------------------------------------------------
struct Result {
  double build_ms = 0, prep_ms = 0, g1_ms = 0, g2_ms = 0, g3_ms = 0, traverse_ms = 0, hit_ns = 0, miss_ns = 0;
  std::int64_t after_prep_rss  = 0;
  std::size_t groups           = 0;  // non-empty groups summed
  std::uint64_t check_groups   = 0;  // sum over groups of (group's tag sum) x hash(grouping, key)
  std::uint64_t check_traverse = 0;  // tag sum over a full traversal
  std::uint64_t sink           = 0;
};

static inline std::uint64_t groupHash(std::uint32_t grouping, std::uint32_t id) {
  return mix((std::uint64_t{grouping} << 32) | id) | 1;
}

// What every arm does with a finished group expression: read its variables (as a solver
// would when the expression is added), fold them into the check, and end it.
static inline void consume(IloExpr& e, std::uint32_t grouping, std::uint32_t id, Result& r) {
  const auto& vars = e.getImpl()->vars;
  if (!vars.empty()) {
    std::uint64_t sum = 0;
    for (const IloNumVarI* v : vars) sum += static_cast<std::uint64_t>(v->ub);
    r.check_groups += sum * groupHash(grouping, id);
    ++r.groups;
  }
  e.end();
}

// --- dreamor ---------------------------------------------------------------------------------
static void runDreamor(const Model& m, const Sample& s, bool mixed, Result& r) {
  using Set   = dreamor::SetOf<K1Col, K2Col, K3Col>;
  using Table = dreamor::concert::VarTable<K1Col, K2Col, K3Col>;
  IloEnv env;
  const auto c2_of = [&m](K1Col k) { return std::span<const E2* const>(m.c2[k->id]); };
  const auto c3_of = [&m](K1Col k) { return std::span<const E3* const>(m.c3[k->id]); };
  const auto kept  = [&m](K1Col a, K2Col b, K3Col c) { return m.keep(a, b, c); };
  const auto tag   = [](K1Col a, K2Col b, K3Col c) { return tagOf(a, b, c); };

  Set sX;
  std::optional<Table> vX;
  r.build_ms = timeMs([&] {
    sX = m.sparse ? dreamor::makeSet<K1Col, K2Col, K3Col>(m.k1s, c2_of, c3_of).where(kept).collect()
                  : dreamor::makeSet<K1Col, K2Col, K3Col>(m.k1s, c2_of, c3_of).collect();
    vX.emplace(env, sX, 0, tag, ILOFLOAT);
  });

  using G1 = decltype(sX.groupBy<K1Col>());
  using G2 = decltype(sX.groupBy<K2Col>());
  using G3 = decltype(sX.groupBy<K3Col>());
  std::unique_ptr<G1> byK1;
  std::unique_ptr<G2> byK2;
  std::unique_ptr<G3> byK3;
  r.prep_ms        = timeMs([&] {
    byK1.reset(new G1{sX.groupBy<K1Col>()});
    if (mixed) {
      byK2.reset(new G2{sX.groupBy<K2Col>()});
      byK3.reset(new G3{sX.groupBy<K3Col>()});
    }
  });
  r.after_prep_rss = residentBytes();

  const Table& table = *vX;

  auto sums = [&](const auto& grouped, std::uint32_t grouping, double& out) {
    out = timeMs([&] {
      for (const auto& [key, rows] : grouped) {
        IloExpr e = dreamor::concert::sumOf(env, rows, table).build();
        consume(e, grouping, key->id, r);
      }
    });
  };
  sums(*byK1, 1, r.g1_ms);
  if (mixed) {
    sums(*byK2, 2, r.g2_ms);
    sums(*byK3, 3, r.g3_ms);
  }
  r.traverse_ms = timeMs([&] {
    for (const auto& [row, var] : table) r.check_traverse += static_cast<std::uint64_t>(var.getUB());
  });
  r.hit_ns      = timeMs([&] {
               for (const auto& [a, b, c] : s.hit) {
                 r.sink += reinterpret_cast<std::uintptr_t>(table(K1Col{a}, K2Col{b}, K3Col{c}).getImpl());
               }
                  }) *
                  1e6 / static_cast<double>(s.hit.size());
  r.miss_ns     = timeMs([&] {
                for (const auto& [a, b, c] : s.miss) r.sink += table.contains(K1Col{a}, K2Col{b}, K3Col{c});
                  }) *
                  1e6 / static_cast<double>(s.miss.size());
}

// --- map -------------------------------------------------------------------------------------
static void runMap(const Model& m, const Sample& s, bool mixed, Result& r) {
  using Key = std::tuple<const E1*, const E2*, const E3*>;
  IloEnv env;
  std::map<Key, IloNumVar> vars;
  r.build_ms = timeMs([&] {
    for (const E1* a : m.k1s)
      for (const E2* b : m.c2[a->id])
        for (const E3* c : m.c3[a->id])
          if (m.keep(a, b, c)) vars[{a, b, c}] = IloNumVar(env, 0, tagOf(a, b, c), ILOFLOAT);
  });

  std::vector<std::vector<const E1*>> owners2, owners3;  // which K1s list this K2 / K3
  r.prep_ms        = timeMs([&] {
    if (!mixed) return;
    owners2.resize(m.e2.size());
    owners3.resize(m.e3.size());
    for (const E1* a : m.k1s) {
      for (const E2* b : m.c2[a->id]) owners2[b->id].push_back(a);
      for (const E3* c : m.c3[a->id]) owners3[c->id].push_back(a);
    }
  });
  r.after_prep_rss = residentBytes();

  auto at = [&vars](const E1* a, const E2* b, const E3* c) { return vars.find({a, b, c})->second; };
  r.g1_ms = timeMs([&] {
    for (const E1* a : m.k1s) {
      IloExpr e(env);
      for (const E2* b : m.c2[a->id])
        for (const E3* c : m.c3[a->id])
          if (m.keep(a, b, c)) e += at(a, b, c);
      consume(e, 1, a->id, r);
    }
  });
  if (mixed) {
    r.g2_ms = timeMs([&] {
      for (const auto& b : m.e2) {
        IloExpr e(env);
        for (const E1* a : owners2[b->id])
          for (const E3* c : m.c3[a->id])
            if (m.keep(a, b.get(), c)) e += at(a, b.get(), c);
        consume(e, 2, b->id, r);
      }
    });
    r.g3_ms = timeMs([&] {
      for (const auto& c : m.e3) {
        IloExpr e(env);
        for (const E1* a : owners3[c->id])
          for (const E2* b : m.c2[a->id])
            if (m.keep(a, b, c.get())) e += at(a, b, c.get());
        consume(e, 3, c->id, r);
      }
    });
  }
  r.traverse_ms = timeMs([&] {
    for (const auto& [key, var] : vars) r.check_traverse += static_cast<std::uint64_t>(var.getUB());
  });
  r.hit_ns      = timeMs([&] {
               for (const auto& [a, b, c] : s.hit) r.sink += reinterpret_cast<std::uintptr_t>(at(a, b, c).getImpl());
                  }) *
                  1e6 / static_cast<double>(s.hit.size());
  r.miss_ns     = timeMs([&] {
                for (const auto& [a, b, c] : s.miss) r.sink += vars.find({a, b, c}) != vars.end();
                  }) *
                  1e6 / static_cast<double>(s.miss.size());
}

// --- flat ------------------------------------------------------------------------------------
static void runFlat(const Model& m, const Sample& s, bool mixed, Result& r) {
  struct Row {
    std::uint32_t i1, i2, i3;
  };
  IloEnv env;
  const std::uint64_t n1 = m.k1s.size(), n2 = m.e2.size(), n3 = m.e3.size();
  auto pack = [n2, n3](std::uint64_t i1, std::uint64_t i2, std::uint64_t i3) { return (i1 * n2 + i2) * n3 + i3; };

  std::vector<Row> rows;
  std::vector<IloNumVar> vars;
  std::unordered_map<std::uint64_t, std::uint32_t> index;
  r.build_ms = timeMs([&] {
    for (const E1* a : m.k1s)
      for (const E2* b : m.c2[a->id])
        for (const E3* c : m.c3[a->id])
          if (m.keep(a, b, c)) {
            rows.push_back({a->id, b->id, c->id});
            vars.emplace_back(env, 0, tagOf(a, b, c), ILOFLOAT);
          }
    index.reserve(rows.size());
    for (std::uint32_t i = 0; i < rows.size(); ++i) index.emplace(pack(rows[i].i1, rows[i].i2, rows[i].i3), i);
  });

  // Rows come out K1 by K1, so the K1 grouping is offsets alone; K2 and K3 are counting sorts.
  std::vector<std::uint32_t> off1, off2, perm2, off3, perm3;
  auto csr = [&rows](std::size_t n, auto keyOf, std::vector<std::uint32_t>& off, std::vector<std::uint32_t>& perm) {
    off.assign(n + 1, 0);
    for (const Row& row : rows) ++off[keyOf(row) + 1];
    for (std::size_t k = 0; k < n; ++k) off[k + 1] += off[k];
    perm.resize(rows.size());
    std::vector<std::uint32_t> next(off.begin(), off.end() - 1);
    for (std::uint32_t i = 0; i < rows.size(); ++i) perm[next[keyOf(rows[i])]++] = i;
  };
  r.prep_ms        = timeMs([&] {
    off1.assign(n1 + 1, 0);
    for (const Row& row : rows) ++off1[row.i1 + 1];
    for (std::size_t k = 0; k < n1; ++k) off1[k + 1] += off1[k];
    if (mixed) {
      csr(n2, [](const Row& row) { return row.i2; }, off2, perm2);
      csr(n3, [](const Row& row) { return row.i3; }, off3, perm3);
    }
  });
  r.after_prep_rss = residentBytes();

  r.g1_ms = timeMs([&] {
    for (std::uint32_t k = 0; k < n1; ++k) {
      IloExpr e(env);
      for (std::uint32_t i = off1[k]; i < off1[k + 1]; ++i) e += vars[i];
      consume(e, 1, k, r);
    }
  });
  if (mixed) {
    auto sums =
        [&](const std::vector<std::uint32_t>& off, const std::vector<std::uint32_t>& perm, std::uint32_t grouping) {
          for (std::uint32_t k = 0; k + 1 < off.size(); ++k) {
            IloExpr e(env);
            for (std::uint32_t p = off[k]; p < off[k + 1]; ++p) e += vars[perm[p]];
            consume(e, grouping, k, r);
          }
        };
    r.g2_ms = timeMs([&] { sums(off2, perm2, 2); });
    r.g3_ms = timeMs([&] { sums(off3, perm3, 3); });
  }
  r.traverse_ms = timeMs([&] {
    for (const IloNumVar& var : vars) r.check_traverse += static_cast<std::uint64_t>(var.getUB());
  });
  r.hit_ns      = timeMs([&] {
               for (const auto& [a, b, c] : s.hit) {
                 r.sink +=
                     reinterpret_cast<std::uintptr_t>(vars[index.find(pack(a->id, b->id, c->id))->second].getImpl());
               }
                  }) *
                  1e6 / static_cast<double>(s.hit.size());
  r.miss_ns     = timeMs([&] {
                for (const auto& [a, b, c] : s.miss) r.sink += index.find(pack(a->id, b->id, c->id)) != index.end();
                  }) *
                  1e6 / static_cast<double>(s.miss.size());
}

// ---------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
  const Args a          = parse(argc, argv);
  const double var_heap = heapBytesPerVar();
  const Model m         = makeModel(a);
  const Sample s        = makeSample(m, a);
  const bool mixed      = a.W == "mixed";

  Result r;
  const std::int64_t before = residentBytes();
  if (a.impl == "dreamor")
    runDreamor(m, s, mixed, r);
  else if (a.impl == "map")
    runMap(m, s, mixed, r);
  else
    runFlat(m, s, mixed, r);
  const double container_bytes =
      static_cast<double>(r.after_prep_rss - before) - var_heap * static_cast<double>(m.rows);

  // The result row, on stdout for run_doe.py - data, so not through the logger.
  const std::string row = std::format(
      "{},{},{},{},{},{},{},{},{},{},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.2f},{:.2f},{:.0f},{},{},{},{},{},{}\n",
      a.impl,
      a.W,
      a.O,
      a.D,
      a.S,
      a.n_target,
      m.rows,
      a.rep,
      a.seed,
      m.k1s.size(),
      r.build_ms,
      r.prep_ms,
      r.g1_ms,
      r.g2_ms,
      r.g3_ms,
      r.traverse_ms,
      r.hit_ns,
      r.miss_ns,
      container_bytes,
      peakBytes(),
      r.groups,
      s.hit.size(),
      r.check_groups,
      r.check_traverse,
      r.sink % 1000);
  std::fputs(row.c_str(), stdout);
  return 0;
}
