// SPDX-License-Identifier: Apache-2.0
// common/measure.hpp - wall, user and system time, page faults and hardware counters per phase; resident memory
#pragma once

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include "dreamor/logger/logger_light.hpp"

// ---------------------------------------------------------------------------------------------
using Clk = std::chrono::steady_clock;
template <typename F>
static double timeMs(F&& f) {
  const auto t = Clk::now();
  f();
  return std::chrono::duration<double, std::milli>(Clk::now() - t).count();
}

// One phase: wall time, the process's user and system time, minor page faults.
constexpr std::size_t kMaxCounters = 8;
struct Phase {
  double wall_ms = 0, user_ms = 0, sys_ms = 0;
  std::int64_t minflt = 0;
  bool ran            = false;
  std::array<std::uint64_t, kMaxCounters> counters{};
};

// --pmu: one group of user-mode counters on this process, read around every phase.
class Pmu {
 public:
  std::vector<std::string> names;

  void open(const std::string& spec) {
    std::size_t from = 0;
    while (from <= spec.size()) {
      const std::size_t to   = std::min(spec.find(',', from), spec.size());
      const std::string item = spec.substr(from, to - from);
      from                   = to + 1;
      if (item.empty()) continue;
      if (names.size() == kMaxCounters) fail("more than 8 counters");
      std::uint32_t type   = PERF_TYPE_HARDWARE;
      std::uint64_t config = 0;
      std::string name     = item;
      if (const std::size_t eq = item.find('='); eq != std::string::npos) {
        name   = item.substr(0, eq);
        type   = PERF_TYPE_RAW;
        config = std::strtoull(item.c_str() + eq + 1, nullptr, 0);
      } else if (item == "cycles") {
        config = PERF_COUNT_HW_CPU_CYCLES;
      } else if (item == "instructions") {
        config = PERF_COUNT_HW_INSTRUCTIONS;
      } else if (item == "branch_miss") {
        config = PERF_COUNT_HW_BRANCH_MISSES;
      } else if (item == "l1d_miss" || item == "dtlb_miss") {
        type   = PERF_TYPE_HW_CACHE;
        config = (item == "l1d_miss" ? PERF_COUNT_HW_CACHE_L1D : PERF_COUNT_HW_CACHE_DTLB) |
                 (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
      } else {
        fail("no counter " + item);
      }
      perf_event_attr attr{};
      attr.size           = sizeof attr;
      attr.type           = type;
      attr.config         = config;
      attr.disabled       = names.empty();
      attr.exclude_kernel = 1;  // perf_event_paranoid 2 allows user mode only
      attr.exclude_hv     = 1;
      attr.read_format    = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
      const long fd       = syscall(SYS_perf_event_open, &attr, 0, -1, names.empty() ? -1 : leader_, 0);
      if (fd < 0) fail("perf_event_open(" + item + "): " + std::strerror(errno));
      if (names.empty()) leader_ = static_cast<int>(fd);
      names.push_back(name);
    }
    if (leader_ >= 0) ioctl(leader_, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
  }
  [[nodiscard]] bool on() const { return leader_ >= 0; }
  // The group's counts now. A group the CPU could not hold all at once would be multiplexed,
  // and its counts estimates: that is refused rather than scaled.
  [[nodiscard]] std::array<std::uint64_t, kMaxCounters> read() const {
    std::array<std::uint64_t, 3 + kMaxCounters> buf{};
    if (::read(leader_, buf.data(), sizeof buf) <= 0) fail("read");
    if (buf[1] != buf[2]) fail("the counters were multiplexed: name fewer");
    std::array<std::uint64_t, kMaxCounters> out{};
    for (std::size_t i = 0; i < names.size(); ++i) out[i] = buf[3 + i];
    return out;
  }

 private:
  [[noreturn]] static void fail(const std::string& why) {
    DREAMOR_LOG_ERROR("--pmu: {}", why);
    std::exit(2);
  }
  int leader_ = -1;
};
static Pmu& pmu() {
  static Pmu p;
  return p;
}
static double msOf(const timeval& t) {
  return static_cast<double>(t.tv_sec) * 1e3 + static_cast<double>(t.tv_usec) / 1e3;
}
template <typename F>
static Phase measure(F&& f) {
  rusage before, after;
  std::array<std::uint64_t, kMaxCounters> counted{};
  if (pmu().on()) counted = pmu().read();
  getrusage(RUSAGE_SELF, &before);
  const double wall = timeMs(std::forward<F>(f));
  getrusage(RUSAGE_SELF, &after);
  if (pmu().on()) {
    const auto now = pmu().read();
    for (std::size_t i = 0; i < kMaxCounters; ++i) counted[i] = now[i] - counted[i];
  }
  return {wall,
          msOf(after.ru_utime) - msOf(before.ru_utime),
          msOf(after.ru_stime) - msOf(before.ru_stime),
          after.ru_minflt - before.ru_minflt,
          true,
          counted};
}

// Signed: the callers subtract one reading from another.
[[maybe_unused]] static std::int64_t residentBytes() {
  std::int64_t pages = 0, resident = 0;
  if (FILE* f = std::fopen("/proc/self/statm", "r")) {
    if (std::fscanf(f, "%" SCNd64 " %" SCNd64, &pages, &resident) != 2) resident = 0;
    std::fclose(f);
  }
  return resident * static_cast<std::int64_t>(sysconf(_SC_PAGESIZE));
}
[[maybe_unused]] static std::int64_t peakBytes() {
  struct rusage u;
  getrusage(RUSAGE_SELF, &u);
  return static_cast<std::int64_t>(u.ru_maxrss) * 1024;
}

// A fresh heap is an allocator's best case (van Kempen & Berger, ISMM '26): blocks of random sizes
// are allocated, a random half freed and the rest kept for the life of the process, so the run
// starts from a heap with holes. The seed is the repetition's: every arm of a repetition starts
// from the same holes, and each repetition from different ones.
[[maybe_unused]] static void preconditionHeap(std::uint64_t seed) {
  if (seed == 0) return;
  std::mt19937_64 g(seed);
  auto* kept = new std::vector<void*>(std::size_t{1} << 16);  // kept, like the blocks it holds
  for (void*& b : *kept) b = std::malloc(16 + g() % 1008);
  for (void*& b : *kept) {
    if (g() & 1) {
      std::free(b);
      b = nullptr;
    }
  }
}
