// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// allOf, anyOf, noneOf, countIf, maxOf, minOf, sumValue, maxElement, minElement and findIf over a set, over a
// group's rows and over no rows: each against what a set made with where() or a plain loop says, the first of
// equal rows for maxElement and minElement, nullptr where there is no row; and over a plain container named as
// one column.
#include <cstddef>
#include <tuple>
#include <vector>

#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {
DREAMOR_VALUE_COLUMN(cI, std::size_t);
DREAMOR_VALUE_COLUMN(cJ, std::size_t);

// A value with < alone and no default constructor: maxOf, minOf, maxElement and minElement compare with < and start
// from the first row's value, as std::ranges::min and max do.
class Cost {
 public:
  explicit Cost(std::size_t value) : value_{value} {}

  [[nodiscard]] std::size_t value() const { return value_; }

  friend bool operator<(const Cost& a, const Cost& b) { return a.value_ < b.value_; }

 private:
  std::size_t value_;
};
}  // namespace

int main() try {
  const std::vector<std::size_t> I{1, 2, 3};
  const std::vector<std::size_t> J{10, 20};
  const auto sIJ =
      dreamor::index_set::relational::from<cI>(I).cross<cJ>(J).build();  // (1,10) (1,20) (2,10) (2,20) (3,10) (3,20)

  const auto even_i     = [](cI i) { return i.get() % 2 == 0; };
  const auto big_j      = [](cJ j) { return j.get() > 15; };
  const auto huge       = [](cI i) { return i.get() > 99; };
  const auto product    = [](cI i, cJ j) { return i.get() * j.get(); };
  const auto i_value    = [](cI i) { return i.get(); };
  const auto minus_i    = [](cI i) { return -static_cast<double>(i.get()); };
  const auto any_column = [](cJ j) { return j.get() > 0; };

  // forall, exists, card - the same answers as a set made with where()
  DREAMOR_CHECK(dreamor::allOf(sIJ, any_column) && !dreamor::allOf(sIJ, even_i));
  DREAMOR_CHECK(dreamor::anyOf(sIJ, even_i) && !dreamor::anyOf(sIJ, huge));
  DREAMOR_CHECK(dreamor::noneOf(sIJ, huge) && !dreamor::noneOf(sIJ, big_j));
  DREAMOR_CHECK(dreamor::countIf(sIJ, even_i) == sIJ.where(even_i).size());
  DREAMOR_CHECK(dreamor::countIf(sIJ, even_i) == 2 && dreamor::countIf(sIJ, huge) == 0);

  // max, min and sum of data
  DREAMOR_CHECK(dreamor::maxOf(sIJ, product) == 60 && dreamor::minOf(sIJ, product) == 10);
  std::size_t total = 0;
  for (const auto& [i, j] : sIJ) {
    total += i.get() * j.get();
  }
  DREAMOR_CHECK(dreamor::sumValue(sIJ, product) == total && total == 180);

  // the row, not the value: the first of equals, as std::ranges::max_element and min_element choose
  const auto* const biggest = dreamor::maxElement(sIJ, i_value);
  DREAMOR_CHECK(biggest != nullptr && *biggest == std::tuple<cI, cJ>{cI{3}, cJ{10}});
  const auto* const smallest = dreamor::minElement(sIJ, i_value);
  DREAMOR_CHECK(smallest != nullptr && *smallest == std::tuple<cI, cJ>{cI{1}, cJ{10}});
  DREAMOR_CHECK(dreamor::maxElement(sIJ, minus_i) == smallest);  // a row of the set, by its address
  const auto* const first_big = dreamor::findIf(sIJ, big_j);
  DREAMOR_CHECK(first_big == &*(sIJ.begin() + 1) && dreamor::findIf(sIJ, huge) == nullptr);

  // a group's rows
  const auto byI     = sIJ.groupBy<cI>();
  const auto twoRows = byI.slice(cI{2});
  DREAMOR_CHECK(dreamor::countIf(twoRows, big_j) == 1 && dreamor::sumValue(twoRows, product) == 60);
  const auto* const row = dreamor::maxElement(twoRows, product);
  DREAMOR_CHECK(row != nullptr && *row == std::tuple<cI, cJ>{cI{2}, cJ{20}});

  // no rows: forall holds, exists does not, the sum is 0, and there is no row
  const std::vector<std::size_t> nothing;
  const auto none = dreamor::index_set::relational::from<cI>(nothing).cross<cJ>(nothing).build();
  DREAMOR_CHECK(dreamor::allOf(none, huge) && !dreamor::anyOf(none, any_column) && dreamor::noneOf(none, any_column));
  DREAMOR_CHECK(dreamor::countIf(none, any_column) == 0 && dreamor::sumValue(none, product) == 0);
  DREAMOR_CHECK(dreamor::maxElement(none, product) == nullptr && dreamor::findIf(none, any_column) == nullptr);

  // a plain container named as one column, as sumExpr<C> takes one - a temporary too
  DREAMOR_CHECK(dreamor::sumValue<cI>(I, i_value) == 6 && dreamor::countIf<cI>(I, even_i) == 1);
  DREAMOR_CHECK(dreamor::maxOf<cI>(I, i_value) == 3 &&
                dreamor::allOf<cJ>(std::vector<std::size_t>{10, 20}, big_j) == false);
  DREAMOR_CHECK(dreamor::anyOf<cJ>(J, big_j) && dreamor::noneOf<cI>(I, huge) && dreamor::minOf<cI>(I, minus_i) < -2.5);

  const auto cost_of = [](cI i, cJ j) { return Cost{i.get() * j.get()}; };
  DREAMOR_CHECK(dreamor::maxOf(sIJ, cost_of).value() == 60 && dreamor::minOf(sIJ, cost_of).value() == 10);
  const auto* most  = dreamor::maxElement(sIJ, cost_of);
  const auto* least = dreamor::minElement(sIJ, cost_of);
  DREAMOR_CHECK(most != nullptr && std::get<cI>(*most) == 3 && std::get<cJ>(*most) == 20);
  DREAMOR_CHECK(least != nullptr && std::get<cI>(*least) == 1 && std::get<cJ>(*least) == 10);

  return report("aggregate");
} catch (...) { return uncaught(); }
