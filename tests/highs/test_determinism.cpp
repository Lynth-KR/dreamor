// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Determinism (support/determinism.hpp) of the HiGHS backend: the example's lot-sizing model on the shared instance,
// as HiGHS holds it after the hand-over (Highs::getLp) - the sense and offset, each column's name, bounds, cost and
// integrality, each row's name and bounds, the matrix as stored. Not solved: a solution's last digits may differ by
// platform (fused multiply-add on arm64), the model may not. Each pass, after the heap is shaken from its seed, must
// print determinism.txt.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include "Highs.h"
#include "dreamor/highs.hpp"
#include "production_planning_highs.hpp"
#include "support/check.hpp"
#include "support/determinism.hpp"

namespace {

std::string modelText(const planning::Instance& data) {
  HighsOptions quiet;
  quiet.output_flag = false;
  dreamor::highs::HighsEnv env{quiet};
  const planning::highs::Model m{env, data};
  m.build(env);
  const HighsLp& lp         = env.highs().getLp();
  std::string text          = std::format("sense {} offset {}\n", static_cast<int>(lp.sense_), lp.offset_);
  const auto at             = [](const auto& values, HighsInt k) { return values[static_cast<std::size_t>(k)]; };
  const auto integrality_of = [&lp, &at](HighsInt k) {
    return lp.integrality_.empty() ? 0 : static_cast<int>(at(lp.integrality_, k));
  };
  text += std::format("== columns ({})\n", lp.num_col_);
  for (HighsInt k = 0; k < lp.num_col_; ++k) {
    text += std::format("{} [{}, {}] {} {}\n",
                        at(lp.col_names_, k),
                        at(lp.col_lower_, k),
                        at(lp.col_upper_, k),
                        at(lp.col_cost_, k),
                        integrality_of(k));
  }
  text += std::format("== rows ({})\n", lp.num_row_);
  for (HighsInt k = 0; k < lp.num_row_; ++k) {
    const std::string name = lp.row_names_.empty() ? "-" : at(lp.row_names_, k);
    text += std::format("{} [{}, {}]\n", name, at(lp.row_lower_, k), at(lp.row_upper_, k));
  }
  const HighsSparseMatrix& a = lp.a_matrix_;
  const HighsInt majors      = a.isRowwise() ? lp.num_row_ : lp.num_col_;
  text += std::format("== matrix ({}, {} entries)\n", a.isRowwise() ? "rowwise" : "colwise", a.numNz());
  for (HighsInt major = 0; major < majors; ++major) {
    text += std::format("{}:", major);
    for (HighsInt k = at(a.start_, major); k < at(a.start_, major + 1); ++k) {
      text += std::format(" {}={}", at(a.index_, k), at(a.value_, k));
    }
    text += '\n';
  }
  return text;
}

}  // namespace

int main() try {
  const auto expected           = std::filesystem::path{__FILE__}.replace_filename("determinism.txt");
  const planning::Instance data = determinism::instance();
  for (const std::uint64_t seed : determinism::kHeapSeeds) {
    const determinism::ShakenHeap shaken{seed};
    determinism::expectText(modelText(data), expected, seed);
  }
  return report("highs determinism");
} catch (...) { return uncaught(); }
