// SPDX-License-Identifier: Apache-2.0
// bench.cpp - measures one implementation (arm) in one cell of the design, one repetition, in
// one process, and prints one CSV line. `bench --columns` prints the header.
//
// Model: a 3-index set (K1, K2, K3). Each K1 has a list of K2s, C2(k1), and a list of K3s,
// C3(k1); the rows are { (k1, k2, k3) : k2 in C2(k1), k3 in C3(k1), keep(k1, k2, k3) }.
// Read it as the lot-sizing example (examples/production_planning.hpp): K1 a machine, K2 an
// item it makes, K3 a period. The work is what that model does with its variable families.
//
// Factors (bench/README.md):
//   N  rows        --N, the design runs 1e5 / 1e6 / 1e7
//   O  K2 owner    shared : all K1s draw K2s from a pool | owned : each K2 belongs to one K1
//   D  density     dense  : every candidate is a row     | sparse: 10% of candidates (predicate)
//   S  fan-out     uniform: same list sizes per K1       | skewed: same mean, Pareto-distributed
//   A  id order    aligned: ids follow the order the data is read and listed
//                  inner  : the K2 and K3 lists are shuffled and the K2 and K3 ids permuted
//                  all    : inner, and the K1 ids permuted as well
//   G  group size  large  : 50 K2s and 100 K3s per K1    | small : 5 and 10, same N
//   F  families    1 | 3 variable families on the same set
//
// Every entity has a serial - its place in the data - and an id - the integer a hand-written
// arm packs into its keys. A permutes ids only: the rows, the variables' bounds and every
// check are the same at every level of A; what changes is how the ids line up with the
// order the rows come in.
//
// Arms - each does the same work the way that style of code naturally does it:
//   dreamor : makeSet(...).where(keep).collect(), F VarTables, groupBy per grouping, sumOf.
//   flat    : hand-optimized: rows (as ids) and one variable vector per family, a
//             std::unordered_map from the packed ids to the row (identity hash), the (K1, K2)
//             groups as runs, the other groupings compacted through an unordered_map on the
//             packed key and laid out by counting sort (CSR).
//   map     : the usual hand-written code (examples/production_planning_plain.hpp): one
//             std::map<tuple<E1*, E2*, E3*>, IloNumVar> per family, filled in one loop. It runs
//             the phases of the stage's scenario only - build, prep_lead (owner lists), g_skip,
//             slice, coef, cross, neighbor - and leaves the others empty; run_doe.py --map runs
//             it in the reference cell.
//
// The solver side is the same in every arm: the stub in bench/stub makes each variable a
// 16-byte heap object and each expression a vector of (variable, coefficient), so it adds the
// same work to every arm. A real solver is no part of it: its insides are a black box whose
// cost moves with the machine, not with the containers. bench/solver/ looks at that apart,
// after the experiment.
//
// Responses, in this order (the stage's operations - the comments name the example's use):
//   build        rows + container + F families
//   prep_contig  grouping by (K1, K2): drops the innermost column, the groups are runs
//   prep_skip    grouping by (K1, K3): drops the middle column     (capacity per machine, period)
//   prep_lead    grouping by (K2, K3): drops the leading column    (balance per item, period)
//   g_contig     one expression per (K1, K2) group, the table as the term
//   g_skip       one expression per (K1, K3) group, coef2(k2) * x by columns (machine usage)
//   g_lead       one expression per (K2, K3) group, the table as the term
//   slice        a driver set of (K2, K3) keys - every pair the rows have, and one absent pair
//                per K2 - one expression per key from the (K2, K3) grouping (stock balance)
//   coef         one expression over every row: coef1(k1) * y, y the last family (objective)
//   cross        every row's x and the y of the same row, found by the row (setup link)
//   neighbor     every row's neighbour (k1, k2, previous k3), looked up by its columns
//   set_*        set operations: select<K2, K3>(); unite, intersect and difference of A (the rows
//                inA keeps) and B (rows generated apart, from the K3 lists moved one period on);
//                a membership of B, filtering the set. Not in the stage's model build, so not in
//                its scenario; map does not run them
//   set_*_merge  flat only, right after each set_*: the same operation the <algorithm> way - the
//                packed keys sorted, then std::set_union / set_intersection / set_difference,
//                std::unique for select, std::binary_search into B for the membership. Its check
//                is the same as set_*'s
//   hit_ns       one lookup of an existing key, random order; miss_ns: an absent key
// Every phase records wall, user and system time and minor page faults: on a fresh process
// much of the time of a build is the kernel handing out pages.
//
// container_bytes: resident-memory growth over build + groupings, minus the variables' heap.
// groups: non-empty (K1, K3) groups. chk_<phase>: the same in every arm iff the arms handed out
// the same variables, with the same coefficients, in the same groups (see consume()).
#include <algorithm>
#include <array>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include "dreamor/concert.hpp"
#include "dreamor/index_set.hpp"
#include "dreamor/logger/logger_light.hpp"

// ---------------------------------------------------------------------------------------------
struct E1 {
  std::uint32_t id, serial;
};
struct E2 {
  std::uint32_t id, serial;
};
struct E3 {
  std::uint32_t id, serial;
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
  std::string impl, O = "shared", D = "dense", S = "uniform", A = "aligned", G = "large";
  std::size_t n_target = 1000000;
  std::size_t families = 1;
  std::uint32_t rep    = 0;
  std::uint64_t seed   = 1;
  std::size_t lookups  = 1000000;
};

static std::string level(const std::string& key, const std::string& value, std::initializer_list<const char*> levels) {
  for (const char* l : levels)
    if (value == l) return value;
  DREAMOR_LOG_ERROR("{}: no level {}", key, value);
  std::exit(2);
}

static Args parse(int argc, char** argv) {
  Args a;
  const auto argCount = static_cast<std::size_t>(argc);
  for (std::size_t i = 1; i + 1 < argCount; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--impl")
      a.impl = level(k, v, {"dreamor", "map", "flat"});
    else if (k == "--O")
      a.O = level(k, v, {"shared", "owned"});
    else if (k == "--D")
      a.D = level(k, v, {"dense", "sparse"});
    else if (k == "--S")
      a.S = level(k, v, {"uniform", "skewed"});
    else if (k == "--A")
      a.A = level(k, v, {"aligned", "inner", "all"});
    else if (k == "--G")
      a.G = level(k, v, {"large", "small"});
    else if (k == "--F")
      a.families = std::stoul(level(k, v, {"1", "3"}));
    else if (k == "--N")
      a.n_target = std::strtoull(v.c_str(), nullptr, 10);
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
  if (a.impl.empty()) {
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

// One phase: wall time, the process's user and system time, minor page faults.
struct Phase {
  double wall_ms = 0, user_ms = 0, sys_ms = 0;
  std::int64_t minflt = 0;
  bool ran            = false;
};
static double msOf(const timeval& t) {
  return static_cast<double>(t.tv_sec) * 1e3 + static_cast<double>(t.tv_usec) / 1e3;
}
template <typename F>
static Phase measure(F&& f) {
  rusage before, after;
  getrusage(RUSAGE_SELF, &before);
  const double wall = timeMs(std::forward<F>(f));
  getrusage(RUSAGE_SELF, &after);
  return {wall,
          msOf(after.ru_utime) - msOf(before.ru_utime),
          msOf(after.ru_stime) - msOf(before.ru_stime),
          after.ru_minflt - before.ru_minflt,
          true};
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
static inline std::uint64_t key3(std::uint32_t s1, std::uint32_t s2, std::uint32_t s3) {
  return (std::uint64_t{s1} << 42) ^ (std::uint64_t{s2} << 21) ^ s3;
}
static inline std::uint64_t pairKey(std::uint32_t x, std::uint32_t y) {
  return (std::uint64_t{x} << 32) | y;
}

// ---------------------------------------------------------------------------------------------
// The model data. Rows are added K1 by K1 until there are n_target of them, so every cell has
// the same row count whatever its density, fan-out and group size.
struct Model {
  std::vector<std::unique_ptr<E1>> e1;  // in serial order, like e2 and e3
  std::vector<std::unique_ptr<E2>> e2;
  std::vector<std::unique_ptr<E3>> e3;      // the last one is in no C3 list: the absent key
  std::vector<const E1*> k1s;               // the order the data is read in
  std::vector<std::vector<const E2*>> c2;   // c2[k1->serial]
  std::vector<std::vector<const E3*>> c3;   // c3[k1->serial]
  std::vector<std::vector<const E3*>> c3b;  // c3b[k1->serial]: each K3 of c3 one period on, for set B
  const E3* absent3 = nullptr;
  bool sparse       = false;
  std::size_t rows  = 0;

  bool keep(const E1* a, const E2* b, const E3* c) const {
    return !sparse || mix(key3(a->serial, b->serial, c->serial)) % 10 == 0;
  }
  // The period before: the K3 one serial earlier, as orderedPeriods.prev(t) in the example.
  const E3* prev3(const E3* c) const { return c->serial == 0 ? nullptr : e3[c->serial - 1].get(); }
  static double coef1(const E1* a) { return 1 + a->serial % 7; }  // a machine's setup cost
  static double coef2(const E2* b) { return 1 + b->serial % 5; }  // an item's machine time
  // The set operations' operands: A = the rows of the set inA keeps, B = rows generated apart.
  static bool inA(std::uint32_t s1, std::uint32_t s3) { return (s1 + s3) % 3 != 0; }
  static bool inB(std::uint32_t s2, std::uint32_t s3) { return (s2 + s3) % 2 == 0; }
};

static Model makeModel(const Args& a) {
  Model m;
  m.sparse = a.D == "sparse";
  std::mt19937_64 g(a.seed * 7919 + 17);
  std::uniform_real_distribution<double> u(0.0, 1.0);
  const bool skewed      = a.S == "skewed";
  const bool owned       = a.O == "owned";
  const std::uint32_t f2 = a.G == "large" ? 50 : 5, f3 = a.G == "large" ? 100 : 10;  // mean list sizes
  // Pareto (alpha = 1.5) weights with mean 1: a few K1s get large lists.
  auto weight = [&]() {
    if (!skewed) return 1.0;
    const double alpha = 1.5, xm = (alpha - 1.0) / alpha;
    return std::min(xm / std::pow(1.0 - u(g), 1.0 / alpha), 50.0);
  };
  const std::size_t pool3 = std::size_t{f3} * 4;  // K3s are drawn from one shared pool
  for (std::size_t i = 0; i <= pool3; ++i) {
    const auto serial = static_cast<std::uint32_t>(i);
    m.e3.push_back(std::make_unique<E3>(E3{serial, serial}));
  }
  m.absent3 = m.e3.back().get();
  std::vector<const E2*> shared2;
  if (!owned) {
    const std::size_t pool2 = std::size_t{f2} * 50;
    for (std::size_t i = 0; i < pool2; ++i) {
      const auto serial = static_cast<std::uint32_t>(i);
      m.e2.push_back(std::make_unique<E2>(E2{serial, serial}));
      shared2.push_back(m.e2.back().get());
    }
  }
  std::size_t rows = 0;
  while (rows < a.n_target) {
    const auto serial = static_cast<std::uint32_t>(m.k1s.size());
    m.e1.push_back(std::make_unique<E1>(E1{serial, serial}));
    const E1* k1 = m.e1.back().get();
    m.k1s.push_back(k1);
    std::size_t n2 = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(f2 * weight())));
    if (!owned) n2 = std::min(n2, shared2.size());
    const std::size_t n3 =
        std::max<std::size_t>(1, std::min(pool3, static_cast<std::size_t>(std::lround(f3 * weight()))));
    std::vector<const E2*> l2;
    if (owned) {
      for (std::size_t j = 0; j < n2; ++j) {
        const auto serial2 = static_cast<std::uint32_t>(m.e2.size());
        m.e2.push_back(std::make_unique<E2>(E2{serial2, serial2}));
        l2.push_back(m.e2.back().get());
      }
    } else {
      const std::size_t start = (std::size_t{serial} * 7) % shared2.size();
      for (std::size_t j = 0; j < n2; ++j) l2.push_back(shared2[(start + j) % shared2.size()]);
    }
    std::vector<const E3*> l3;
    const std::size_t start3 = mix(serial) % pool3;
    for (std::size_t j = 0; j < n3; ++j) l3.push_back(m.e3[(start3 + j) % pool3].get());
    for (const E2* b : l2)
      for (const E3* c : l3) rows += m.keep(k1, b, c);
    m.c2.push_back(std::move(l2));
    m.c3.push_back(std::move(l3));
  }
  m.rows = rows;

  // A: list order and ids, from a stream of its own so that the rows above do not depend on it.
  std::mt19937_64 h(a.seed * 6151 + 29);
  auto permuteIds = [&h](auto& entities, std::size_t count) {
    std::vector<std::uint32_t> ids(count);
    std::iota(ids.begin(), ids.end(), 0U);
    std::shuffle(ids.begin(), ids.end(), h);
    for (std::size_t i = 0; i < count; ++i) entities[i]->id = ids[i];
  };
  if (a.A != "aligned") {
    for (auto& l : m.c2) std::shuffle(l.begin(), l.end(), h);
    for (auto& l : m.c3) std::shuffle(l.begin(), l.end(), h);
    permuteIds(m.e2, m.e2.size());
    permuteIds(m.e3, m.e3.size() - 1);  // the absent key keeps the last id
  }
  if (a.A == "all") permuteIds(m.e1, m.e1.size());
  const std::size_t pool = m.e3.size() - 1;  // the absent key is in no list
  for (const auto& l : m.c3) {
    std::vector<const E3*> moved;
    moved.reserve(l.size());
    for (const E3* c : l) moved.push_back(m.e3[(c->serial + 1) % pool].get());
    m.c3b.push_back(std::move(moved));
  }
  return m;
}

// Each variable carries a tag fixed by its row's key and its family, as its upper bound. The
// arms are then checked against each other by the tags they hand out, independently of
// addresses and of ids.
static inline double tagOf(std::uint32_t s1, std::uint32_t s2, std::uint32_t s3, std::size_t family) {
  return static_cast<double>(mix(key3(s1, s2, s3) + family * 0x51afd7ed558ccd1ULL) % 1000003);
}

// What a hand-written model keeps beside the entities - arrays by id - and the driver set of
// the slice phase. Built before any arm runs, and outside the measurements.
struct Side {
  static constexpr std::uint32_t kNone = 0xffffffffU;
  std::vector<std::uint32_t> serial1, serial2, serial3;  // by id
  std::vector<double> coef1, coef2;                      // by id
  std::vector<std::uint32_t> prev3;                      // by id: the id of the K3 before, or kNone
  // (K2, K3) keys, K2 by K2 and K3 by K3 in serial order (the example's stock set): every pair
  // the rows have, and one absent pair per K2.
  std::vector<std::pair<const E2*, const E3*>> driver;
};

static Side makeSide(const Model& m) {
  Side s;
  s.serial1.resize(m.e1.size());
  s.coef1.resize(m.e1.size());
  for (const auto& e : m.e1) {
    s.serial1[e->id] = e->serial;
    s.coef1[e->id]   = Model::coef1(e.get());
  }
  s.serial2.resize(m.e2.size());
  s.coef2.resize(m.e2.size());
  for (const auto& e : m.e2) {
    s.serial2[e->id] = e->serial;
    s.coef2[e->id]   = Model::coef2(e.get());
  }
  s.serial3.resize(m.e3.size());
  s.prev3.resize(m.e3.size());
  for (const auto& e : m.e3) {
    s.serial3[e->id] = e->serial;
    const E3* p      = m.prev3(e.get());
    s.prev3[e->id]   = p ? p->id : Side::kNone;
  }
  std::vector<std::vector<std::uint32_t>> k3s_of(m.e2.size());  // by K2 serial: K3 serials
  for (const E1* a : m.k1s)
    for (const E2* b : m.c2[a->serial])
      for (const E3* c : m.c3[a->serial])
        if (m.keep(a, b, c)) k3s_of[b->serial].push_back(c->serial);
  for (std::size_t b = 0; b < k3s_of.size(); ++b) {
    auto& l = k3s_of[b];
    std::sort(l.begin(), l.end());
    l.erase(std::unique(l.begin(), l.end()), l.end());
    for (const std::uint32_t c : l) s.driver.emplace_back(m.e2[b].get(), m.e3[c].get());
    s.driver.emplace_back(m.e2[b].get(), m.absent3);
  }
  return s;
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
    const auto& l2 = m.c2[k1->serial];
    const auto& l3 = m.c3[k1->serial];
    const E2* b    = l2[g() % l2.size()];
    const E3* c    = l3[g() % l3.size()];
    if (!m.keep(k1, b, c)) continue;
    s.hit.emplace_back(k1, b, c);
    s.miss.emplace_back(k1, b, m.absent3);
  }
  return s;
}

// ---------------------------------------------------------------------------------------------
enum PhaseId : std::uint32_t {
  kBuild,
  kPrepContig,
  kPrepSkip,
  kPrepLead,
  kGContig,
  kGSkip,
  kGLead,
  kSlice,
  kCoef,
  kCross,
  kNeighbor,
  kSetSelect,
  kSetSelectMerge,
  kSetUnite,
  kSetUniteMerge,
  kSetIntersect,
  kSetIntersectMerge,
  kSetDifference,
  kSetDifferenceMerge,
  kSetMember,
  kSetMemberMerge,
  kPhaseCount
};
constexpr std::array<const char*, kPhaseCount> kPhaseNames = {"build",
                                                              "prep_contig",
                                                              "prep_skip",
                                                              "prep_lead",
                                                              "g_contig",
                                                              "g_skip",
                                                              "g_lead",
                                                              "slice",
                                                              "coef",
                                                              "cross",
                                                              "neighbor",
                                                              "set_select",
                                                              "set_select_merge",
                                                              "set_unite",
                                                              "set_unite_merge",
                                                              "set_intersect",
                                                              "set_intersect_merge",
                                                              "set_difference",
                                                              "set_difference_merge",
                                                              "set_member",
                                                              "set_member_merge"};
// The phases that hand out variables or rows, and so have a check.
constexpr std::array<PhaseId, 17> kCheckedPhases = {kGContig,
                                                    kGSkip,
                                                    kGLead,
                                                    kSlice,
                                                    kCoef,
                                                    kCross,
                                                    kNeighbor,
                                                    kSetSelect,
                                                    kSetSelectMerge,
                                                    kSetUnite,
                                                    kSetUniteMerge,
                                                    kSetIntersect,
                                                    kSetIntersectMerge,
                                                    kSetDifference,
                                                    kSetDifferenceMerge,
                                                    kSetMember,
                                                    kSetMemberMerge};

// A set operation's result in its check: the rows' hashes summed - order-free, as the arms keep
// their own order - and the row count.
static inline std::uint64_t rowHash(std::uint32_t s1, std::uint32_t s2, std::uint32_t s3) {
  return mix(key3(s1, s2, s3));
}
constexpr std::uint64_t kCountWeight = 0x9e3779b97f4a7c15ULL;

struct Result {
  std::array<Phase, kPhaseCount> phases;
  double hit_ns = 0, miss_ns = 0;
  std::int64_t after_prep_rss = 0;
  std::size_t groups          = 0;  // non-empty (K1, K3) groups
  std::array<std::uint64_t, kPhaseCount> check{};
  std::uint64_t sink = 0;
};

static inline std::uint64_t groupHash(PhaseId phase, std::uint64_t key) {
  return mix(key * 0x9e3779b97f4a7c15ULL ^ phase) | 1;
}

// What every arm does with a finished expression: read its terms (as a solver would when the
// expression is added), fold them into the phase's check under a hash of the group's key, and
// end it.
static void consume(IloExpr& e, PhaseId phase, std::uint64_t key, Result& r) {
  std::uint64_t sum = 0;
  const IloExprI& x = *e.getImpl();
  for (std::size_t i = 0; i < x.vars.size(); ++i) {
    sum += static_cast<std::uint64_t>(x.vars[i]->ub) * static_cast<std::uint64_t>(x.coefs[i]);
  }
  const std::size_t terms = x.vars.size();
  if (terms != 0) {
    r.check[phase] += sum * groupHash(phase, key);
    r.groups += phase == kGSkip;
  }
  e.end();
}

static inline std::uint64_t ubOf(const IloNumVar& v) {
  return static_cast<std::uint64_t>(v.getUB());
}

// --- dreamor ---------------------------------------------------------------------------------
static void runDreamor(const Model& m, const Side& side, const Sample& s, std::size_t families, Result& r) {
  using Set   = dreamor::SetOf<K1Col, K2Col, K3Col>;
  using Table = dreamor::concert::VarTable<K1Col, K2Col, K3Col>;
  IloEnv env;
  const auto c2_of = [&m](K1Col k) { return std::span<const E2* const>(m.c2[k->serial]); };
  const auto c3_of = [&m](K1Col k) { return std::span<const E3* const>(m.c3[k->serial]); };
  const auto kept  = [&m](K1Col a, K2Col b, K3Col c) { return m.keep(a, b, c); };

  Set sX;
  std::vector<Table> tables;
  r.phases[kBuild] = measure([&] {
    sX = m.sparse ? dreamor::makeSet<K1Col, K2Col, K3Col>(m.k1s, c2_of, c3_of).where(kept).collect()
                  : dreamor::makeSet<K1Col, K2Col, K3Col>(m.k1s, c2_of, c3_of).collect();
    tables.reserve(families);
    for (std::size_t f = 0; f < families; ++f) {
      const auto tag = [f](K1Col a, K2Col b, K3Col c) { return tagOf(a->serial, b->serial, c->serial, f); };
      tables.emplace_back(env, sX, 0, tag, ILOFLOAT);
    }
  });

  using GContig = decltype(sX.groupBy<K1Col, K2Col>());
  using GSkip   = decltype(sX.groupBy<K1Col, K3Col>());
  using GLead   = decltype(sX.groupBy<K2Col, K3Col>());
  std::unique_ptr<GContig> byContig;
  std::unique_ptr<GSkip> bySkip;
  std::unique_ptr<GLead> byLead;
  r.phases[kPrepContig] = measure([&] { byContig.reset(new GContig{sX.groupBy<K1Col, K2Col>()}); });
  r.phases[kPrepSkip]   = measure([&] { bySkip.reset(new GSkip{sX.groupBy<K1Col, K3Col>()}); });
  r.phases[kPrepLead]   = measure([&] { byLead.reset(new GLead{sX.groupBy<K2Col, K3Col>()}); });
  r.after_prep_rss      = residentBytes();

  const Table& vX     = tables.front();
  const Table& vY     = tables.back();
  r.phases[kGContig]  = measure([&] {
    for (const auto& [a, b, rows] : *byContig) {
      IloExpr e = dreamor::concert::sumOf(env, rows, vX).build();
      consume(e, kGContig, pairKey(a->serial, b->serial), r);
    }
  });
  const auto usage    = [&vX](K1Col a, K2Col b, K3Col c) { return Model::coef2(b) * vX(a, b, c); };
  r.phases[kGSkip]    = measure([&] {
    for (const auto& [a, c, rows] : *bySkip) {
      IloExpr e = dreamor::concert::sumOf(env, rows, usage).build();
      consume(e, kGSkip, pairKey(a->serial, c->serial), r);
    }
  });
  r.phases[kGLead]    = measure([&] {
    for (const auto& [b, c, rows] : *byLead) {
      IloExpr e = dreamor::concert::sumOf(env, rows, vX).build();
      consume(e, kGLead, pairKey(b->serial, c->serial), r);
    }
  });
  r.phases[kSlice]    = measure([&] {
    for (const auto& [b, c] : side.driver) {
      IloExpr e = dreamor::concert::sumOf(env, byLead->slice(K2Col{b}, K3Col{c}), vX).build();
      consume(e, kSlice, pairKey(b->serial, c->serial), r);
    }
  });
  const auto cost     = [&vY](K1Col a, K2Col b, K3Col c) { return Model::coef1(a) * vY(a, b, c); };
  r.phases[kCoef]     = measure([&] {
    IloExpr e = dreamor::concert::sumOf(env, sX, cost).build();
    consume(e, kCoef, 0, r);
  });
  r.phases[kCross]    = measure([&] {
    for (const auto& [row, x] : vX) r.check[kCross] += ubOf(x) + 3 * ubOf(vY(row));
  });
  r.phases[kNeighbor] = measure([&] {
    for (const auto& [a, b, c] : sX) {
      const E3* p = m.prev3(c);
      if (p != nullptr && vX.contains(a, b, K3Col{p})) r.check[kNeighbor] += ubOf(vX(a, b, K3Col{p}));
    }
  });

  // Set operations on A = sX.where(inA) and B, generated apart from the K3 lists moved one
  // period on and filtered by inB. A and B are not measured; each result is checked after its
  // phase and dropped before the next.
  const auto c3b_of = [&m](K1Col k) { return std::span<const E3* const>(m.c3b[k->serial]); };
  const auto in_a   = [](K1Col a, K3Col c) { return Model::inA(a->serial, c->serial); };
  const auto in_b   = [](K2Col b, K3Col c) { return Model::inB(b->serial, c->serial); };
  const Set sA      = sX.where(in_a);
  const Set sB = m.sparse
                     ? dreamor::makeSet<K1Col, K2Col, K3Col>(m.k1s, c2_of, c3b_of).where(kept).where(in_b).collect()
                     : dreamor::makeSet<K1Col, K2Col, K3Col>(m.k1s, c2_of, c3b_of).where(in_b).collect();
  const auto checkRows = [&r](PhaseId phase, const Set& set) {
    for (const auto& [a, b, c] : set) r.check[phase] += rowHash(a->serial, b->serial, c->serial);
    r.check[phase] += set.size() * kCountWeight;
  };
  {
    dreamor::SetOf<K2Col, K3Col> projected;
    r.phases[kSetSelect] = measure([&] { projected = sX.select<K2Col, K3Col>(); });
    for (const auto& [b, c] : projected) r.check[kSetSelect] += mix(pairKey(b->serial, c->serial));
    r.check[kSetSelect] += projected.size() * kCountWeight;
  }
  {
    Set out;
    r.phases[kSetUnite] = measure([&] { out = dreamor::unite(sA, sB); });
    checkRows(kSetUnite, out);
  }
  {
    Set out;
    r.phases[kSetIntersect] = measure([&] { out = dreamor::intersect(sA, sB); });
    checkRows(kSetIntersect, out);
  }
  {
    Set out;
    r.phases[kSetDifference] = measure([&] { out = dreamor::difference(sA, sB); });
    checkRows(kSetDifference, out);
  }
  {
    Set out;
    r.phases[kSetMember] = measure([&] {
      const auto in_set_b = sB.membership();
      out                 = sX.where(in_set_b);
    });
    checkRows(kSetMember, out);
  }
  r.hit_ns  = timeMs([&] {
               for (const auto& [a, b, c] : s.hit) {
                 r.sink += reinterpret_cast<std::uintptr_t>(vX(K1Col{a}, K2Col{b}, K3Col{c}).getImpl());
               }
              }) *
              1e6 / static_cast<double>(s.hit.size());
  r.miss_ns = timeMs([&] {
                for (const auto& [a, b, c] : s.miss) r.sink += vX.contains(K1Col{a}, K2Col{b}, K3Col{c});
              }) *
              1e6 / static_cast<double>(s.miss.size());
  env.end();
}

// --- flat ------------------------------------------------------------------------------------
static void runFlat(const Model& m, const Side& side, const Sample& s, std::size_t families, Result& r) {
  struct Row {
    std::uint32_t i1, i2, i3;
  };
  // A grouping: group keys in first-appearance order, CSR offsets into perm (row numbers), and
  // the key -> group index the slices use.
  struct Csr {
    std::vector<std::uint64_t> keys;
    std::vector<std::uint32_t> off, perm;
    std::unordered_map<std::uint64_t, std::uint32_t> index;
  };
  IloEnv env;
  const std::uint64_t n2 = m.e2.size(), n3 = m.e3.size();
  auto pack = [n2, n3](std::uint64_t i1, std::uint64_t i2, std::uint64_t i3) { return (i1 * n2 + i2) * n3 + i3; };

  std::vector<Row> rows;
  std::vector<std::vector<IloNumVar>> vars(families);
  std::unordered_map<std::uint64_t, std::uint32_t> index;
  r.phases[kBuild] = measure([&] {
    for (const E1* a : m.k1s)
      for (const E2* b : m.c2[a->serial])
        for (const E3* c : m.c3[a->serial])
          if (m.keep(a, b, c)) rows.push_back({a->id, b->id, c->id});
    for (std::size_t f = 0; f < families; ++f) {
      vars[f].reserve(rows.size());
      for (const Row& row : rows) {
        vars[f].emplace_back(
            env, 0, tagOf(side.serial1[row.i1], side.serial2[row.i2], side.serial3[row.i3], f), ILOFLOAT);
      }
    }
    index.reserve(rows.size());
    for (std::uint32_t i = 0; i < rows.size(); ++i) index.emplace(pack(rows[i].i1, rows[i].i2, rows[i].i3), i);
  });

  auto csr = [&rows](Csr& g, auto keyOf) {
    std::vector<std::uint32_t> group_of(rows.size());
    for (std::uint32_t i = 0; i < rows.size(); ++i) {
      const std::uint64_t key   = keyOf(rows[i]);
      const auto [it, is_fresh] = g.index.try_emplace(key, static_cast<std::uint32_t>(g.keys.size()));
      if (is_fresh) {
        g.keys.push_back(key);
        g.off.push_back(0);
      }
      group_of[i] = it->second;
      ++g.off[it->second];
    }
    std::uint32_t running = 0;
    for (std::uint32_t& o : g.off) running += std::exchange(o, running);
    g.off.push_back(running);
    g.perm.resize(rows.size());
    std::vector<std::uint32_t> next(g.off.begin(), g.off.end() - 1);
    for (std::uint32_t i = 0; i < rows.size(); ++i) g.perm[next[group_of[i]]++] = i;
  };
  // (K1, K2): rows come out K1 by K1 and, inside, K2 by K2, so the groups are runs.
  std::vector<std::uint32_t> off_contig;
  Csr skip, lead;
  r.phases[kPrepContig] = measure([&] {
    for (std::uint32_t i = 0; i < rows.size(); ++i) {
      if (i == 0 || rows[i].i1 != rows[i - 1].i1 || rows[i].i2 != rows[i - 1].i2) off_contig.push_back(i);
    }
    off_contig.push_back(static_cast<std::uint32_t>(rows.size()));
  });
  r.phases[kPrepSkip]   = measure([&] { csr(skip, [n3](const Row& row) { return row.i1 * n3 + row.i3; }); });
  r.phases[kPrepLead]   = measure([&] { csr(lead, [n3](const Row& row) { return row.i2 * n3 + row.i3; }); });
  r.after_prep_rss      = residentBytes();

  const std::vector<IloNumVar>& x = vars.front();
  const std::vector<IloNumVar>& y = vars.back();
  r.phases[kGContig]              = measure([&] {
    for (std::size_t k = 0; k + 1 < off_contig.size(); ++k) {
      IloExpr e(env);
      for (std::uint32_t i = off_contig[k]; i < off_contig[k + 1]; ++i) e += x[i];
      const Row& first = rows[off_contig[k]];
      consume(e, kGContig, pairKey(side.serial1[first.i1], side.serial2[first.i2]), r);
    }
  });
  r.phases[kGSkip]                = measure([&] {
    for (std::size_t k = 0; k < skip.keys.size(); ++k) {
      IloExpr e(env);
      for (std::uint32_t p = skip.off[k]; p < skip.off[k + 1]; ++p) {
        const std::uint32_t i = skip.perm[p];
        e += side.coef2[rows[i].i2] * x[i];
      }
      const std::uint64_t key = skip.keys[k];
      consume(e, kGSkip, pairKey(side.serial1[key / n3], side.serial3[key % n3]), r);
    }
  });
  r.phases[kGLead]                = measure([&] {
    for (std::size_t k = 0; k < lead.keys.size(); ++k) {
      IloExpr e(env);
      for (std::uint32_t p = lead.off[k]; p < lead.off[k + 1]; ++p) e += x[lead.perm[p]];
      const std::uint64_t key = lead.keys[k];
      consume(e, kGLead, pairKey(side.serial2[key / n3], side.serial3[key % n3]), r);
    }
  });
  r.phases[kSlice]                = measure([&] {
    for (const auto& [b, c] : side.driver) {
      IloExpr e(env);
      if (const auto it = lead.index.find(b->id * n3 + c->id); it != lead.index.end()) {
        for (std::uint32_t p = lead.off[it->second]; p < lead.off[it->second + 1]; ++p) e += x[lead.perm[p]];
      }
      consume(e, kSlice, pairKey(b->serial, c->serial), r);
    }
  });
  r.phases[kCoef]                 = measure([&] {
    IloExpr e(env);
    for (std::uint32_t i = 0; i < rows.size(); ++i) e += side.coef1[rows[i].i1] * y[i];
    consume(e, kCoef, 0, r);
  });
  r.phases[kCross]                = measure([&] {
    for (std::uint32_t i = 0; i < rows.size(); ++i) r.check[kCross] += ubOf(x[i]) + 3 * ubOf(y[i]);
  });
  r.phases[kNeighbor]             = measure([&] {
    for (const Row& row : rows) {
      const std::uint32_t p = side.prev3[row.i3];
      if (p == Side::kNone) continue;
      if (const auto it = index.find(pack(row.i1, row.i2, p)); it != index.end()) {
        r.check[kNeighbor] += ubOf(x[it->second]);
      }
    }
  });

  // Set operations: A and B as rows of ids, a hash set on the packed ids where one is needed.
  // A filters distinct rows, so it has no repeats to drop - the hand-coder knows. Each one runs
  // again the <algorithm> way right after: the packed keys sorted and merged, in key order.
  std::vector<Row> set_a, set_b;
  for (const Row& row : rows)
    if (Model::inA(side.serial1[row.i1], side.serial3[row.i3])) set_a.push_back(row);
  for (const E1* a : m.k1s)
    for (const E2* b : m.c2[a->serial])
      for (const E3* c : m.c3b[a->serial])
        if (m.keep(a, b, c) && Model::inB(b->serial, c->serial)) set_b.push_back({a->id, b->id, c->id});
  const auto key       = [&pack](const Row& row) { return pack(row.i1, row.i2, row.i3); };
  const auto checkRows = [&r, &side](PhaseId phase, const std::vector<Row>& out) {
    for (const Row& row : out)
      r.check[phase] += rowHash(side.serial1[row.i1], side.serial2[row.i2], side.serial3[row.i3]);
    r.check[phase] += out.size() * kCountWeight;
  };
  const auto checkKeys = [&r, &side, n2, n3](PhaseId phase, const std::vector<std::uint64_t>& out) {
    for (const std::uint64_t k : out)
      r.check[phase] += rowHash(side.serial1[k / n3 / n2], side.serial2[k / n3 % n2], side.serial3[k % n3]);
    r.check[phase] += out.size() * kCountWeight;
  };
  const auto setOf = [&key](const std::vector<Row>& in) {
    std::unordered_set<std::uint64_t> keys;
    keys.reserve(in.size());
    for (const Row& row : in) keys.insert(key(row));
    return keys;
  };
  const auto sortedKeys = [&key](const std::vector<Row>& in) {
    std::vector<std::uint64_t> keys;
    keys.reserve(in.size());
    for (const Row& row : in) keys.push_back(key(row));
    std::sort(keys.begin(), keys.end());
    return keys;
  };
  {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> projected;
    r.phases[kSetSelect] = measure([&] {
      std::unordered_set<std::uint64_t> seen;
      seen.reserve(rows.size());
      for (const Row& row : rows)
        if (seen.insert(row.i2 * n3 + row.i3).second) projected.emplace_back(row.i2, row.i3);
    });
    for (const auto& [i2, i3] : projected) r.check[kSetSelect] += mix(pairKey(side.serial2[i2], side.serial3[i3]));
    r.check[kSetSelect] += projected.size() * kCountWeight;
  }
  {
    std::vector<std::uint64_t> projected;
    r.phases[kSetSelectMerge] = measure([&] {
      projected.reserve(rows.size());
      for (const Row& row : rows) projected.push_back(row.i2 * n3 + row.i3);
      std::sort(projected.begin(), projected.end());
      projected.erase(std::unique(projected.begin(), projected.end()), projected.end());
    });
    for (const std::uint64_t k : projected) {
      r.check[kSetSelectMerge] += mix(pairKey(side.serial2[k / n3], side.serial3[k % n3]));
    }
    r.check[kSetSelectMerge] += projected.size() * kCountWeight;
  }
  {
    std::vector<Row> out;
    r.phases[kSetUnite] = measure([&] {
      std::unordered_set<std::uint64_t> seen;
      seen.reserve(set_a.size() + set_b.size());
      for (const auto* part : {&set_a, &set_b})
        for (const Row& row : *part)
          if (seen.insert(key(row)).second) out.push_back(row);
    });
    checkRows(kSetUnite, out);
  }
  {
    std::vector<std::uint64_t> out;
    r.phases[kSetUniteMerge] = measure([&] {
      const auto keys_a = sortedKeys(set_a), keys_b = sortedKeys(set_b);
      std::set_union(keys_a.begin(), keys_a.end(), keys_b.begin(), keys_b.end(), std::back_inserter(out));
    });
    checkKeys(kSetUniteMerge, out);
  }
  {
    std::vector<Row> out;
    r.phases[kSetIntersect] = measure([&] {
      const auto in_b = setOf(set_b);
      for (const Row& row : set_a)
        if (in_b.contains(key(row))) out.push_back(row);
    });
    checkRows(kSetIntersect, out);
  }
  {
    std::vector<std::uint64_t> out;
    r.phases[kSetIntersectMerge] = measure([&] {
      const auto keys_a = sortedKeys(set_a), keys_b = sortedKeys(set_b);
      std::set_intersection(keys_a.begin(), keys_a.end(), keys_b.begin(), keys_b.end(), std::back_inserter(out));
    });
    checkKeys(kSetIntersectMerge, out);
  }
  {
    std::vector<Row> out;
    r.phases[kSetDifference] = measure([&] {
      const auto in_b = setOf(set_b);
      for (const Row& row : set_a)
        if (!in_b.contains(key(row))) out.push_back(row);
    });
    checkRows(kSetDifference, out);
  }
  {
    std::vector<std::uint64_t> out;
    r.phases[kSetDifferenceMerge] = measure([&] {
      const auto keys_a = sortedKeys(set_a), keys_b = sortedKeys(set_b);
      std::set_difference(keys_a.begin(), keys_a.end(), keys_b.begin(), keys_b.end(), std::back_inserter(out));
    });
    checkKeys(kSetDifferenceMerge, out);
  }
  {
    std::vector<Row> out;
    r.phases[kSetMember] = measure([&] {
      const auto in_b = setOf(set_b);
      for (const Row& row : rows)
        if (in_b.contains(key(row))) out.push_back(row);
    });
    checkRows(kSetMember, out);
  }
  {
    std::vector<Row> out;
    r.phases[kSetMemberMerge] = measure([&] {
      const auto in_b = sortedKeys(set_b);
      for (const Row& row : rows)
        if (std::binary_search(in_b.begin(), in_b.end(), key(row))) out.push_back(row);
    });
    checkRows(kSetMemberMerge, out);
  }
  r.hit_ns  = timeMs([&] {
               for (const auto& [a, b, c] : s.hit) {
                 r.sink += reinterpret_cast<std::uintptr_t>(x[index.find(pack(a->id, b->id, c->id))->second].getImpl());
               }
              }) *
              1e6 / static_cast<double>(s.hit.size());
  r.miss_ns = timeMs([&] {
                for (const auto& [a, b, c] : s.miss) r.sink += index.find(pack(a->id, b->id, c->id)) != index.end();
              }) *
              1e6 / static_cast<double>(s.miss.size());
  env.end();
}

// --- map -------------------------------------------------------------------------------------
static void runMap(const Model& m, const Side& side, const Sample& s, std::size_t families, Result& r) {
  using Key = std::tuple<const E1*, const E2*, const E3*>;
  IloEnv env;
  std::vector<std::map<Key, IloNumVar>> vars(families);
  r.phases[kBuild] = measure([&] {
    for (const E1* a : m.k1s)
      for (const E2* b : m.c2[a->serial])
        for (const E3* c : m.c3[a->serial])
          if (m.keep(a, b, c))
            for (std::size_t f = 0; f < families; ++f)
              vars[f][{a, b, c}] = IloNumVar(env, 0, tagOf(a->serial, b->serial, c->serial, f), ILOFLOAT);
  });

  std::vector<std::vector<const E1*>> owners2;  // which K1s list this K2
  r.phases[kPrepLead] = measure([&] {
    owners2.resize(m.e2.size());
    for (const E1* a : m.k1s)
      for (const E2* b : m.c2[a->serial]) owners2[b->serial].push_back(a);
  });
  r.after_prep_rss    = residentBytes();

  auto& x             = vars.front();
  auto& y             = vars.back();
  r.phases[kGSkip]    = measure([&] {
    for (const E1* a : m.k1s)
      for (const E3* c : m.c3[a->serial]) {
        IloExpr e(env);
        for (const E2* b : m.c2[a->serial])
          if (m.keep(a, b, c)) e += Model::coef2(b) * x.find({a, b, c})->second;
        consume(e, kGSkip, pairKey(a->serial, c->serial), r);
      }
  });
  r.phases[kSlice]    = measure([&] {
    for (const auto& [b, c] : side.driver) {
      IloExpr e(env);
      for (const E1* a : owners2[b->serial])
        if (const auto it = x.find({a, b, c}); it != x.end()) e += it->second;
      consume(e, kSlice, pairKey(b->serial, c->serial), r);
    }
  });
  r.phases[kCoef]     = measure([&] {
    IloExpr e(env);
    for (const auto& [key, var] : y) e += Model::coef1(std::get<0>(key)) * var;
    consume(e, kCoef, 0, r);
  });
  r.phases[kCross]    = measure([&] {
    for (const auto& [key, var] : x) r.check[kCross] += ubOf(var) + 3 * ubOf(y.find(key)->second);
  });
  r.phases[kNeighbor] = measure([&] {
    for (const auto& [key, var] : x) {
      const auto& [a, b, c] = key;
      const E3* p           = m.prev3(c);
      if (p == nullptr) continue;
      if (const auto it = x.find({a, b, p}); it != x.end()) r.check[kNeighbor] += ubOf(it->second);
    }
  });
  r.hit_ns            = timeMs([&] {
               for (const auto& key : s.hit) r.sink += reinterpret_cast<std::uintptr_t>(x.find(key)->second.getImpl());
                        }) *
                        1e6 / static_cast<double>(s.hit.size());
  r.miss_ns           = timeMs([&] {
                for (const auto& key : s.miss) r.sink += x.find(key) != x.end();
                        }) *
                        1e6 / static_cast<double>(s.miss.size());
  env.end();
}

// ---------------------------------------------------------------------------------------------
// The output row as (column, value) pairs: one list, so the header (`--columns`) and the row
// cannot drift apart. A phase an arm does not run is left empty.
struct Fields {
  std::vector<std::string> names, values;
  void add(std::string name, std::string value) {
    names.push_back(std::move(name));
    values.push_back(std::move(value));
  }
};

struct RunInfo {
  std::size_t rows = 0, k1_count = 0, lookups = 0;
  double container_bytes = 0;
  std::int64_t peak      = 0;
  int cpu_start = -1, cpu_end = -1;
};

static Fields fields(const Args& a, const RunInfo& info, const Result& r) {
  Fields f;
  f.add("impl", a.impl);
  f.add("N_target", std::to_string(a.n_target));
  f.add("N", std::to_string(info.rows));
  f.add("O", a.O);
  f.add("D", a.D);
  f.add("S", a.S);
  f.add("A", a.A);
  f.add("G", a.G);
  f.add("F", std::to_string(a.families));
  f.add("rep", std::to_string(a.rep));
  f.add("seed", std::to_string(a.seed));
  f.add("k1_count", std::to_string(info.k1_count));
  for (std::size_t p = 0; p < kPhaseCount; ++p) {
    const Phase& ph     = r.phases[p];
    const std::string n = kPhaseNames[p];
    f.add(n + "_ms", ph.ran ? std::format("{:.3f}", ph.wall_ms) : "");
    f.add(n + "_user_ms", ph.ran ? std::format("{:.3f}", ph.user_ms) : "");
    f.add(n + "_sys_ms", ph.ran ? std::format("{:.3f}", ph.sys_ms) : "");
    f.add(n + "_minflt", ph.ran ? std::to_string(ph.minflt) : "");
  }
  f.add("hit_ns", std::format("{:.2f}", r.hit_ns));
  f.add("miss_ns", std::format("{:.2f}", r.miss_ns));
  f.add("lookups", std::to_string(info.lookups));
  f.add("container_bytes", std::format("{:.0f}", info.container_bytes));
  f.add("peak_rss_bytes", std::to_string(info.peak));
  f.add("groups", r.phases[kGSkip].ran ? std::to_string(r.groups) : "");
  for (const PhaseId p : kCheckedPhases) {
    f.add(std::string("chk_") + kPhaseNames[p], r.phases[p].ran ? std::to_string(r.check[p]) : "");
  }
  f.add("sink", std::to_string(r.sink % 1000));
  f.add("cpu_start", std::to_string(info.cpu_start));
  f.add("cpu_end", std::to_string(info.cpu_end));
  return f;
}

static std::string joined(const std::vector<std::string>& v) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + v[i];
  return out + "\n";
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--columns") {
    std::fputs(joined(fields(Args{}, RunInfo{}, Result{}).names).c_str(), stdout);
    return 0;
  }
  const Args a = parse(argc, argv);
  RunInfo info;
  info.cpu_start        = sched_getcpu();
  const double var_heap = heapBytesPerVar();
  const Model m         = makeModel(a);
  const Sample s        = makeSample(m, a);
  const Side side       = makeSide(m);

  Result r;
  const std::int64_t before = residentBytes();
  if (a.impl == "dreamor")
    runDreamor(m, side, s, a.families, r);
  else if (a.impl == "map")
    runMap(m, side, s, a.families, r);
  else
    runFlat(m, side, s, a.families, r);
  info.rows     = m.rows;
  info.k1_count = m.k1s.size();
  info.lookups  = s.hit.size();
  info.container_bytes =
      static_cast<double>(r.after_prep_rss - before) - var_heap * static_cast<double>(m.rows * a.families);
  info.peak    = peakBytes();
  info.cpu_end = sched_getcpu();

  // The result row, on stdout for run_doe.py - data, so not through the logger.
  std::fputs(joined(fields(a, info, r).values).c_str(), stdout);
  return 0;
}
