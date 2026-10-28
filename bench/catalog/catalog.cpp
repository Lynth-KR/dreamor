// SPDX-License-Identifier: Apache-2.0
// catalog.cpp - measures one arm of one operation family in one cell, one repetition, in one
// process, and prints one JSON line. run.py designs the experiment and reads the lines.
//
// A family is a group of operations on the same data, measured as phases one after another
// (README.md). Every arm builds the same data the same way before its first phase, so the
// heap an arm starts from does not depend on the arm.
//
//   catalog --family lookup|group|setop|slice|make|order|data --impl <arm> --k 2|3|5 --N <rows>
//           --O shared|owned --D dense|sparse --S uniform|skewed --G large|small
//           --L ordered|shuffled --I aligned|permuted [--rep r] [--seed s] [--lookups n]
//           [--heap-seed h] [--phases a,b,...] [--pmu <events>]
//   catalog --family data --impl <arm> --N <entities> --key int|string --access inserted|random ...
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include <sched.h>

#include "../common/measure.hpp"
#include "../common/result.hpp"
#include "data.hpp"
#include "dreamor/logger/logger_light.hpp"
#include "entities.hpp"
#include "group.hpp"
#include "lookup.hpp"
#include "make.hpp"
#include "order.hpp"
#include "setop.hpp"
#include "slice.hpp"

namespace {

struct Args {
  std::string family, impl, pmu;
  std::string O = "shared", D = "dense", S = "uniform", G = "large", L = "ordered", I = "aligned";
  std::string key = "int", access = "inserted";  // the data family's factors
  Cell cell;
  std::uint32_t rep       = 0;
  std::size_t lookups     = 1000000;
  std::uint64_t heap_seed = 0;
  std::string phases;
};

[[noreturn]] void usage(const std::string& why) {
  DREAMOR_LOG_ERROR("{}", why);
  std::exit(2);
}

std::string level(const std::string& key, const std::string& value, std::initializer_list<const char*> levels) {
  for (const char* l : levels) {
    if (value == l) { return value; }
  }
  usage(key + ": no level " + value);
}

Args parse(int argc, char** argv) {
  Args a;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--family") {
      a.family = level(k, v, {"lookup", "group", "setop", "slice", "make", "order", "data"});
    } else if (k == "--impl") {
      a.impl = v;
    } else if (k == "--k") {
      a.cell.k = std::stoul(level(k, v, {"2", "3", "5"}));
    } else if (k == "--N") {
      a.cell.n_target = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--O") {
      a.O = level(k, v, {"shared", "owned"});
    } else if (k == "--D") {
      a.D = level(k, v, {"dense", "sparse"});
    } else if (k == "--S") {
      a.S = level(k, v, {"uniform", "skewed"});
    } else if (k == "--G") {
      a.G = level(k, v, {"large", "small"});
    } else if (k == "--L") {
      a.L = level(k, v, {"ordered", "shuffled"});
    } else if (k == "--I") {
      a.I = level(k, v, {"aligned", "permuted"});
    } else if (k == "--key") {
      a.key = level(k, v, {"int", "string"});
    } else if (k == "--access") {
      a.access = level(k, v, {"inserted", "random"});
    } else if (k == "--rep") {
      a.rep = static_cast<std::uint32_t>(std::stoul(v));
    } else if (k == "--seed") {
      a.cell.seed = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--lookups") {
      a.lookups = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--pmu") {
      a.pmu = v;
    } else if (k == "--heap-seed") {
      a.heap_seed = std::strtoull(v.c_str(), nullptr, 10);
    } else if (k == "--phases") {
      a.phases = v;
    } else {
      usage("unknown option " + k);
    }
  }
  if (a.family.empty() || a.impl.empty()) { usage("--family and --impl are required"); }
  a.cell.owned    = a.O == "owned";
  a.cell.sparse   = a.D == "sparse";
  a.cell.skewed   = a.S == "skewed";
  a.cell.small    = a.G == "small";
  a.cell.shuffled = a.L == "shuffled";
  a.cell.permuted = a.I == "permuted";
  return a;
}

}  // namespace

int main(int argc, char** argv) {
  const Args a        = parse(argc, argv);
  const int cpu_start = sched_getcpu();
  Result r;
  if (!a.phases.empty()) {
    std::set<std::string> chosen;
    for (std::size_t from = 0; from <= a.phases.size();) {
      const std::size_t to = std::min(a.phases.find(',', from), a.phases.size());
      chosen.insert(a.phases.substr(from, to - from));
      from = to + 1;
    }
    r.select(std::move(chosen));
  }
  Data d;
  Samples s;
  r.setup("heap", [&] { preconditionHeap(a.heap_seed); });
  // The data family has entities of its own, not rows.
  if (a.family != "data") {
    r.setup("data", [&] { d = makeData(a.cell); });
  }
  // Only the lookup family looks keys up: the others skip drawing them.
  if (a.family == "lookup") {
    r.setup("samples", [&] { s = makeSamples(d, a.lookups, a.cell.seed); });
  }
  if (!a.pmu.empty()) { pmu().open(a.pmu); }
  if (a.family == "lookup") {
    lookup::run(a.impl, d, s, r);
  } else if (a.family == "group") {
    group::run(a.impl, d, s, r);
  } else if (a.family == "setop") {
    setop::run(a.impl, d, s, r);
  } else if (a.family == "slice") {
    slice::run(a.impl, d, a.cell.seed, r);
  } else if (a.family == "make") {
    make::run(a.impl, d, s, r);
  } else if (a.family == "order") {
    order::run(a.impl, d, s, r);
  } else {
    entities::run(a.impl, a.key, a.access, a.cell.n_target, a.cell.seed, r);
  }

  std::printf(
      "{\"family\": \"%s\", \"impl\": \"%s\", \"k\": %zu, \"N_target\": %zu, \"O\": \"%s\", \"D\": \"%s\", "
      "\"S\": \"%s\", \"G\": \"%s\", \"L\": \"%s\", \"I\": \"%s\", \"key\": \"%s\", \"access\": \"%s\", \"rep\": %u, \"seed\": %llu, \"heap_seed\": %llu, "
      "\"rows\": %zu, "
      "\"k1_count\": %zu, \"lookups\": %zu, \"peak_rss_bytes\": %lld, \"cpu_start\": %d, \"cpu_end\": %d, %s}\n",
      a.family.c_str(),
      a.impl.c_str(),
      a.cell.k,
      a.cell.n_target,
      a.O.c_str(),
      a.D.c_str(),
      a.S.c_str(),
      a.G.c_str(),
      a.L.c_str(),
      a.I.c_str(),
      a.key.c_str(),
      a.access.c_str(),
      a.rep,
      static_cast<unsigned long long>(a.cell.seed),
      static_cast<unsigned long long>(a.heap_seed),
      d.rows,
      d.k1s.size(),
      s.hit.size(),
      static_cast<long long>(peakBytes()),
      cpu_start,
      sched_getcpu(),
      r.json().c_str());
  // Nothing is measured from here on: the data goes with the process, not one allocation at a time.
  std::fflush(stdout);
  std::_Exit(0);
}
