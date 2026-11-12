// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Same data, same result, byte for byte, on every platform and every run. One fixed lot-sizing instance, drawn by a
// generator of its own (std's distributions draw differently in each standard library), put through the set layer
// and a backend's model, printed as text and held against a file beside the test. The keys hash differently on each
// platform and run - an entity's text key by std::hash, a Ref (a route's key) by its address - and each pass first
// shakes the heap from a seed of its own, so an order that leaked a hash or an address shows as a difference.
// DREAMOR_DETERMINISM_WRITE=1 writes the file instead of reading it.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "production_planning_data.hpp"
#include "support/check.hpp"

namespace determinism {

// splitmix64: the same numbers from any compiler and standard library.
class Draw {
 public:
  explicit Draw(std::uint64_t seed) : state_{seed} {}

  std::uint64_t next() {
    state_ += 0x9e3779b97f4a7c15ULL;
    std::uint64_t z = state_;
    z               = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    z               = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31U);
  }

  int below(int n) { return static_cast<int>(next() % static_cast<std::uint64_t>(n)); }

 private:
  std::uint64_t state_;
};

inline constexpr int kItems    = 12;
inline constexpr int kMachines = 4;
inline constexpr int kPeriods  = 4;

// Items named out of their sort order (so insertion, sort and hash orders all differ), dyadic numbers (exact in
// binary, printed the same everywhere), demand that is zero in some periods, routes on about 60% of the pairs.
inline planning::Instance instance() {
  Draw draw{20261112};
  planning::Instance in;
  for (int t = 0; t < kPeriods; ++t) {
    in.periods.push_back(t);
  }
  constexpr std::array<std::string_view, 3> kKinds{"nut", "frame", "bolt"};
  for (int k = 0; k < kItems; ++k) {
    planning::ItemRecord item{
        .name   = std::format("{}-{:02}", kKinds[static_cast<std::size_t>(k % 3)], kItems - k),
        .time   = 1.0 + (draw.below(8) / 4.0),
        .hold   = 0.5 + (draw.below(4) / 4.0),
        .demand = {},
    };
    for (int t = 0; t < kPeriods; ++t) {
      item.demand.push_back(draw.below(10) < 3 ? 0.0 : 10.0 + draw.below(30));
    }
    in.items.push_back(std::move(item));
  }
  for (int k = 0; k < kMachines; ++k) {
    in.machines.push_back(planning::MachineRecord{
        .name     = std::format("M{}", (k * 3) % kMachines),
        .capacity = 40.0 + draw.below(40),
        .setup    = 20.0 + draw.below(30),
    });
    for (int item = 0; item < kItems; ++item) {
      if (draw.below(10) < 6) { in.routes.push_back(planning::RouteRecord{.machine = k, .item = item}); }
    }
  }
  return in;
}

// Holes in the heap before a pass: blocks of drawn sizes, half of them freed, the rest kept while the pass runs. Seed
// 0 leaves the heap as it is.
class ShakenHeap {
 public:
  explicit ShakenHeap(std::uint64_t seed) {
    if (seed == 0) { return; }
    Draw draw{seed};
    std::vector<std::vector<std::byte>> blocks(std::size_t{1} << 14U);
    for (auto& block : blocks) {
      block.resize(16 + static_cast<std::size_t>(draw.below(1008)));
    }
    for (auto& block : blocks) {
      if ((draw.next() & 1U) != 0) { kept_.push_back(std::move(block)); }
    }
  }

 private:
  std::vector<std::vector<std::byte>> kept_;
};

inline constexpr std::array<std::uint64_t, 3> kHeapSeeds{0, 1, 2};

// A row as text: its columns' std::format, a space between.
template <typename Row>
std::string rowText(const Row& row) {
  std::string text;
  std::apply(
      [&text](const auto&... columns) { ((text += text.empty() ? "" : " ", text += std::format("{}", columns)), ...); },
      row);
  return text;
}

inline std::string readText(const std::filesystem::path& path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  if (!file) { return {}; }
  std::string text(static_cast<std::size_t>(file.tellg()), '\0');
  file.seekg(0);
  file.read(text.data(), static_cast<std::streamsize>(text.size()));
  std::erase(text, '\r');  // a checkout that turned the file's line ends into CRLF
  return text;
}

inline void writeText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream file{path, std::ios::binary};
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// The next line of text from at, and at moved past it; nullopt at the end.
inline std::optional<std::string_view> nextLine(std::string_view text, std::size_t& at) {
  if (at >= text.size()) { return std::nullopt; }
  const std::size_t end       = std::min(text.find('\n', at), text.size());
  const std::string_view line = text.substr(at, end - at);
  at                          = end + 1;
  return line;
}

// Whether DREAMOR_DETERMINISM_WRITE is set. The MSVC CRT deprecates getenv, so Windows takes _dupenv_s (as
// differential.hpp's environmentText does).
inline bool writeRequested() {
#ifdef _WIN32
  char* value      = nullptr;
  std::size_t size = 0;
  const bool set   = _dupenv_s(&value, &size, "DREAMOR_DETERMINISM_WRITE") == 0 && value != nullptr;
  std::free(value);
  return set;
#else
  return std::getenv("DREAMOR_DETERMINISM_WRITE") != nullptr;
#endif
}

// The pass's text against the expected file: the first line that differs, and the whole text in <name>.actual.txt.
inline void expectText(const std::string& text, const std::filesystem::path& expected, std::uint64_t heap_seed) {
  if (writeRequested()) {
    writeText(expected, text);
    return;
  }
  const std::string want = readText(expected);
  DREAMOR_CHECK(!want.empty());
  if (text == want) {
    DREAMOR_CHECK(true);
    return;
  }
  std::size_t got_at  = 0;
  std::size_t want_at = 0;
  for (std::size_t number = 1;; ++number) {
    const auto got_line  = nextLine(text, got_at);
    const auto want_line = nextLine(want, want_at);
    if (!got_line && !want_line) { break; }
    if (got_line != want_line) {
      std::fprintf(stderr,
                   "heap seed %llu, line %zu:\n  expected: %s\n  got:      %s\n",
                   static_cast<unsigned long long>(heap_seed),
                   number,
                   want_line ? std::string{*want_line}.c_str() : "(end)",
                   got_line ? std::string{*got_line}.c_str() : "(end)");
      break;
    }
  }
  const std::filesystem::path actual = expected.stem().string() + ".actual.txt";
  writeText(actual, text);
  std::fprintf(stderr, "the whole text: %s\n", std::filesystem::absolute(actual).string().c_str());
  DREAMOR_CHECK(text == want);
}

}  // namespace determinism
