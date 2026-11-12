// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// interval::closed, open, closedOpen and openClosed as sources of a column: their values, an interval whose ends cross,
// the column's integer type whatever the ends', a temporary handed over as it is, beside other sources.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {
DREAMOR_VALUE_COLUMN(cCut, std::size_t);
DREAMOR_VALUE_COLUMN(cPeriod, int);
DREAMOR_VALUE_COLUMN(cDay, std::uint8_t);
DREAMOR_VALUE_COLUMN(cLine, std::size_t);
}  // namespace

namespace {

// The column's values, as plain numbers.
template <typename Column, typename Set>
std::vector<typename Column::value_type> valuesOf(const Set& set) {
  std::vector<typename Column::value_type> out;
  out.reserve(set.size());
  for (const auto& row : set) {
    out.push_back(std::get<Column>(row));
  }
  return out;
}

}  // namespace

int main() try {
  namespace iv = dreamor::interval;
  // the four bounds, on one pair of ends
  DREAMOR_CHECK((valuesOf<cPeriod>(dreamor::index_set::relational::from<cPeriod>(iv::closed(1, 4)).build()) ==
                 std::vector{1, 2, 3, 4}));
  DREAMOR_CHECK(
      (valuesOf<cPeriod>(dreamor::index_set::relational::from<cPeriod>(iv::open(1, 4)).build()) == std::vector{2, 3}));
  DREAMOR_CHECK((valuesOf<cPeriod>(dreamor::index_set::relational::from<cPeriod>(iv::closedOpen(1, 4)).build()) ==
                 std::vector{1, 2, 3}));
  DREAMOR_CHECK((valuesOf<cPeriod>(dreamor::index_set::relational::from<cPeriod>(iv::openClosed(1, 4)).build()) ==
                 std::vector{2, 3, 4}));
  DREAMOR_CHECK((valuesOf<cPeriod>(dreamor::index_set::relational::from<cPeriod>(iv::closed(-2, 1)).build()) ==
                 std::vector{-2, -1, 0, 1}));

  // ends that cross, or meet: closed(1, 0) is empty, closed(1, 1) is {1}
  DREAMOR_CHECK(dreamor::index_set::relational::from<cPeriod>(iv::closed(1, 0)).build().empty());
  DREAMOR_CHECK(dreamor::index_set::relational::from<cPeriod>(iv::closed(1, 1)).build().size() == 1);
  DREAMOR_CHECK(dreamor::index_set::relational::from<cPeriod>(iv::open(1, 2)).build().empty());
  DREAMOR_CHECK(dreamor::index_set::relational::from<cPeriod>(iv::closedOpen(3, 3)).build().empty());
  DREAMOR_CHECK(dreamor::index_set::relational::from<cPeriod>(iv::openClosed(5, 1)).build().empty());

  // int ends fill a std::size_t or a std::uint8_t column; ends of two types meet in their common one
  constexpr std::size_t kCuts = 161;
  const auto sCUTS            = dreamor::index_set::relational::from<cCut>(iv::closed(0, 160)).build();
  DREAMOR_CHECK(sCUTS.size() == kCuts && valuesOf<cCut>(sCUTS).back() == 160);
  DREAMOR_CHECK(dreamor::index_set::relational::from<cCut>(iv::closedOpen(0, kCuts)).build() == sCUTS);
  DREAMOR_CHECK((valuesOf<cDay>(dreamor::index_set::relational::from<cDay>(iv::closed(1, 3)).build()) ==
                 std::vector<std::uint8_t>{1, 2, 3}));

  // beside other sources: a product with a container and a predicate, and two intervals
  const std::vector<std::size_t> lines{10, 20};
  const auto sRUN = dreamor::index_set::relational::from<cLine>(lines)
                        .cross<cPeriod>(iv::closed(1, 3))
                        .where([](cPeriod t) { return t.get() != 2; })
                        .build();
  DREAMOR_CHECK(sRUN.size() == 4);
  const auto sTWO =
      dreamor::index_set::relational::from<cPeriod>(iv::closed(1, 3)).cross<cCut>(iv::closedOpen(0, 2)).build();
  DREAMOR_CHECK(sTWO.size() == 6);
  return report("interval");
} catch (...) { return ::uncaught(); }
