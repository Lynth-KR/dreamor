// SPDX-License-Identifier: Apache-2.0
// common/result.hpp - one run's result: every phase's times, counters and check, as one JSON line
#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "measure.hpp"

class Result {
 public:
  // --phases: measure these only. The phases later ones stand on - build, collect, load, index, prep_* - run
  // all the same, and are reported only when chosen.
  void select(std::set<std::string> phases) { chosen_ = std::move(phases); }
  [[nodiscard]] bool chosen(const std::string& phase) const { return chosen_.empty() || chosen_.contains(phase); }

  template <typename F>
  void measure(const std::string& phase, F&& f) {
    const bool prerequisite =
        phase == "build" || phase == "collect" || phase == "load" || phase == "index" || phase.starts_with("prep_");
    if (!chosen(phase) && !prerequisite) { return; }
    Phase p = ::measure(std::forward<F>(f));
    if (chosen(phase)) { phases_.emplace_back(phase, p); }
  }
  // A setup stage before the phases: timed, never part of a ratio.
  template <typename F>
  void setup(const std::string& stage, F&& f) {
    setup_.emplace_back(stage, timeMs(std::forward<F>(f)));
  }
  // What the phase handed out, folded into one number that every arm must agree on.
  void check(const std::string& phase, std::uint64_t value) {
    if (chosen(phase)) { checks_.emplace_back(phase, value); }
  }
  // The arm cannot run this cell (an array that does not fit, say): no phase is reported.
  void skip(std::string reason) { skipped_ = std::move(reason); }
  std::uint64_t sink = 0;

  // {"phases": {...}, "checks": {...}, "skipped": ...} - the caller writes the run's own fields.
  [[nodiscard]] std::string json() const {
    std::string out = "\"skipped\": " + (skipped_.empty() ? std::string("null") : quoted(skipped_));
    out += ", \"phases\": {";
    for (std::size_t i = 0; i < phases_.size(); ++i) {
      const auto& [name, p] = phases_[i];
      char buf[256];
      std::snprintf(buf,
                    sizeof buf,
                    "%s\"%s\": {\"ms\": %.3f, \"user_ms\": %.3f, \"sys_ms\": %.3f, \"minflt\": %" PRId64,
                    i ? ", " : "",
                    name.c_str(),
                    p.wall_ms,
                    p.user_ms,
                    p.sys_ms,
                    static_cast<std::int64_t>(p.minflt));
      out += buf;
      if (pmu().on()) {
        out += ", \"counters\": {";
        for (std::size_t c = 0; c < pmu().names.size(); ++c) {
          out += (c ? ", " : "") + quoted(pmu().names[c]) + ": " + std::to_string(p.counters[c]);
        }
        out += "}";
      }
      out += "}";
    }
    out += "}, \"setup_ms\": {";
    for (std::size_t i = 0; i < setup_.size(); ++i) {
      char buf[96];
      std::snprintf(buf, sizeof buf, "%s\"%s\": %.3f", i ? ", " : "", setup_[i].first.c_str(), setup_[i].second);
      out += buf;
    }
    out += "}, \"checks\": {";
    for (std::size_t i = 0; i < checks_.size(); ++i) {
      out += (i ? ", " : "") + quoted(checks_[i].first) + ": \"" + std::to_string(checks_[i].second) + "\"";
    }
    return out + "}, \"sink\": " + std::to_string(sink % 1000);
  }

 private:
  static std::string quoted(const std::string& s) { return "\"" + s + "\""; }
  std::vector<std::pair<std::string, Phase>> phases_;
  std::vector<std::pair<std::string, std::uint64_t>> checks_;
  std::string skipped_;
  std::set<std::string> chosen_;
  std::vector<std::pair<std::string, double>> setup_;
};
