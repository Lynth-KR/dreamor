// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// catalog/hand/make.hpp - the hand-written arms of the make family, which ../make.hpp describes.
#pragma once

#include "../make.hpp"
#include "arm.hpp"

namespace make {

enum class Way { kIdentity, kMixed, kSort, kMap };

static void runHand(Way way, const Data& d, Result& r) {
  const std::size_t k = d.k, last = d.last();
  // The rows of A, k - 1 entities each, and the (k1, period) pairs of B, in generation order. At k = 1 both are empty.
  std::vector<const Ent*> a_rows;
  std::vector<std::pair<const Ent*, const Ent*>> b_rows;
  if (k > 1) {
    std::array<const Ent*, kMaxColumns> row{};
    const auto walk = [&](auto& self, std::size_t j) -> void {
      if (j == last) {
        a_rows.insert(a_rows.end(), row.begin(), row.begin() + static_cast<std::ptrdiff_t>(last));
        return;
      }
      for (const Ent* e : d.lists[j][row[0]->serial]) {
        row[j] = e;
        self(self, j + 1);
      }
    };
    for (const Ent* a : d.k1s) {
      row[0] = a;
      walk(walk, 1);
      for (const Ent* t : d.lists[last][a->serial]) {
        b_rows.emplace_back(a, t);
      }
    }
  }

  IloEnv env;
  std::vector<const Ent*> rows;  // The entities of the rows, k for each row.
  r.measure("collect", [&] {
    d.forEachRow([&](const std::array<const Ent*, kMaxColumns>& row) {
      rows.insert(rows.end(), row.begin(), row.begin() + static_cast<std::ptrdiff_t>(k));
    });
  });
  const std::size_t n  = rows.size() / k;
  const auto serialsAt = [&](const std::vector<const Ent*>& from, std::size_t i) {
    std::array<std::uint32_t, kMaxColumns> s{};
    for (std::size_t j = 0; j < k; ++j) {
      s[j] = from[i * k + j]->serial;
    }
    return s;
  };
  const auto setCheck = [&](const std::vector<const Ent*>& from) {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < from.size() / k; ++i) {
      sum += rowCheck(serialsAt(from, i), k);
    }
    return sum;
  };
  r.check("collect", setCheck(rows));

  std::vector<IloNumVar> x, named;
  r.measure("vars", [&] {
    x.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      x.emplace_back(env, 0, tagOf(serialsAt(rows, i), k, 0), ILOFLOAT);
    }
  });
  {
    std::uint64_t sum = 0;
    for (const IloNumVar& v : x) {
      sum += ubOf(v);
    }
    r.check("vars", sum);
  }
  r.measure("vars_named", [&] {
    named.reserve(n);
    std::array<char, 256> name{};
    for (std::size_t i = 0; i < n; ++i) {
      char* at = name.data();
      *at++    = 'x';
      for (std::size_t j = 0; j < k; ++j) {
        *at++ = '_';
        at    = std::to_chars(at, name.data() + name.size() - 1, rows[i * k + j]->id).ptr;
      }
      *at = '\0';
      named.emplace_back(env, 0, tagOf(serialsAt(rows, i), k, 0), ILOFLOAT, name.data());
    }
  });
  {
    std::uint64_t sum = 0;
    for (const IloNumVar& v : named) {
      sum += ubOf(v);
    }
    r.check("vars_named", sum);
  }

  if (k == 1) {
    env.end();
    return;
  }
  std::vector<const Ent*> joined;  // The entities of the rows, k for each row.
  r.measure("join", [&] {
    const std::size_t a_width = last;
    std::array<std::uint32_t, kMaxColumns> serials{};
    std::array<const Ent*, kMaxColumns> row{};
    // Emits row i of A with each period from first up to end, if the predicate keeps the row. period_of returns the
    // period of an element.
    const auto emit = [&](std::size_t i, auto first, auto end, auto period_of) {
      for (std::size_t j = 0; j < a_width; ++j) {
        row[j]     = a_rows[i * a_width + j];
        serials[j] = row[j]->serial;
      }
      for (auto it = first; it != end; ++it) {
        row[last]     = period_of(*it);
        serials[last] = row[last]->serial;
        if (d.keep(serials)) { joined.insert(joined.end(), row.begin(), row.begin() + static_cast<std::ptrdiff_t>(k)); }
      }
    };
    const std::size_t a_count = a_rows.size() / a_width;
    if (way == Way::kIdentity || way == Way::kMixed) {
      const auto hashed = [&](auto& group_of) {
        // Groups the periods of B by k1 into one array (CSR), and finds the group of a k1 through its id.
        std::vector<std::uint32_t> start, count;
        std::vector<const Ent*> periods(b_rows.size());
        std::vector<std::uint32_t> group(b_rows.size());
        group_of.reserve(b_rows.size());
        for (std::size_t i = 0; i < b_rows.size(); ++i) {
          const auto [it, fresh] = group_of.try_emplace(b_rows[i].first->id, static_cast<std::uint32_t>(count.size()));
          if (fresh) { count.push_back(0); }
          group[i] = it->second;
          ++count[it->second];
        }
        start.resize(count.size() + 1);
        for (std::size_t c = 0; c < count.size(); ++c) {
          start[c + 1] = start[c] + count[c];
        }
        std::vector<std::uint32_t> next(start.begin(), start.end() - 1);
        for (std::size_t i = 0; i < b_rows.size(); ++i) {
          periods[next[group[i]]++] = b_rows[i].second;
        }
        for (std::size_t i = 0; i < a_count; ++i) {
          const auto it = group_of.find(a_rows[i * a_width]->id);
          if (it == group_of.end()) { continue; }
          emit(i, periods.begin() + start[it->second], periods.begin() + start[it->second + 1], [](const Ent* t) {
            return t;
          });
        }
      };
      if (way == Way::kIdentity) {
        std::unordered_map<std::uint32_t, std::uint32_t> group_of;
        hashed(group_of);
      } else {
        std::unordered_map<std::uint32_t, std::uint32_t, MixedHash> group_of;
        hashed(group_of);
      }
    } else if (way == Way::kSort) {
      std::vector<std::pair<std::uint32_t, const Ent*>> sorted(b_rows.size());
      for (std::size_t i = 0; i < b_rows.size(); ++i) {
        sorted[i] = {b_rows[i].first->id, b_rows[i].second};
      }
      std::stable_sort(sorted.begin(), sorted.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
      for (std::size_t i = 0; i < a_count; ++i) {
        const std::uint32_t id  = a_rows[i * a_width]->id;
        const auto [first, end] = std::equal_range(sorted.begin(),
                                                   sorted.end(),
                                                   std::pair<std::uint32_t, const Ent*>{id, nullptr},
                                                   [](const auto& p, const auto& q) { return p.first < q.first; });
        emit(i, first, end, [](const auto& p) { return p.second; });
      }
    } else {
      std::map<const Ent*, std::vector<const Ent*>> periods_of;
      for (const auto& [a, t] : b_rows) {
        periods_of[a].push_back(t);
      }
      for (std::size_t i = 0; i < a_count; ++i) {
        const auto it = periods_of.find(a_rows[i * a_width]);
        if (it == periods_of.end()) { continue; }
        emit(i, it->second.begin(), it->second.end(), [](const Ent* t) { return t; });
      }
    }
  });
  r.check("join", setCheck(joined));
  env.end();
}

static void run(const std::string& impl, const Data& d, const Samples&, Result& r) {
  if (impl == "identity") {
    runHand(Way::kIdentity, d, r);
  } else if (impl == "mixed") {
    runHand(Way::kMixed, d, r);
  } else if (impl == "sort") {
    runHand(Way::kSort, d, r);
  } else if (impl == "map") {
    runHand(Way::kMap, d, r);
  } else {
    r.skip("no arm " + impl);
  }
}

}  // namespace make
