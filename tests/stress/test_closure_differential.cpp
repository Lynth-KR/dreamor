// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// transitiveClosure against Floyd-Warshall, over a few hundred random relations from a fixed seed: graphs with
// cycles and self-loops, and acyclic ones dense with diamonds, as a bill of materials is; nodes as integers and as
// Refs to entities. The reference is another algorithm - shortest walks of one edge or more over a matrix, min-plus
// - and checks the rows (every pair a walk joins, each once), their order (the sources as the relation first lists
// them, each one's nodes nearest first) and the relation's own edges among them.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

DREAMOR_VALUE_COLUMN(cFrom, int);
DREAMOR_VALUE_COLUMN(cTo, int);

struct Node {
  int id = 0;

  [[nodiscard]] int primaryKey() const { return id; }
};

DREAMOR_ENTITY_COLUMN(cSource, Node);
DREAMOR_ENTITY_COLUMN(cTarget, Node);

constexpr std::size_t kUnreached = std::numeric_limits<std::size_t>::max();

// A relation as node numbers 0..n-1, its edges in the relation's order.
struct Graph {
  std::size_t nodes = 0;
  std::vector<std::pair<std::size_t, std::size_t>> edges;
};

Graph randomGraph(std::mt19937& g, bool acyclic) {
  Graph graph;
  graph.nodes          = std::uniform_int_distribution<std::size_t>{0, 24}(g);
  const double density = std::array<double, 3>{0.05, 0.15, 0.35}[std::uniform_int_distribution<std::size_t>{0, 2}(g)];
  std::bernoulli_distribution edge{density};
  std::vector<std::size_t> rank(graph.nodes);  // an acyclic graph's edges go up this order, a shuffled one
  std::iota(rank.begin(), rank.end(), std::size_t{0});
  std::ranges::shuffle(rank, g);
  for (std::size_t a = 0; a < graph.nodes; ++a) {
    for (std::size_t b = 0; b < graph.nodes; ++b) {
      if ((!acyclic || rank[a] < rank[b]) && edge(g)) { graph.edges.emplace_back(a, b); }
    }
  }
  std::ranges::shuffle(graph.edges, g);
  return graph;
}

// The shortest walk of one edge or more from each node to each, min-plus over the matrix: Floyd-Warshall with no zero
// on the diagonal, so a node reaches itself only round a cycle.
std::vector<std::vector<std::size_t>> walkLengths(const Graph& graph) {
  std::vector<std::vector<std::size_t>> length(graph.nodes, std::vector<std::size_t>(graph.nodes, kUnreached));
  for (const auto& [a, b] : graph.edges) {
    length[a][b] = 1;
  }
  for (std::size_t k = 0; k < graph.nodes; ++k) {
    for (std::size_t a = 0; a < graph.nodes; ++a) {
      if (length[a][k] == kUnreached) { continue; }
      for (std::size_t b = 0; b < graph.nodes; ++b) {
        if (length[k][b] != kUnreached) { length[a][b] = std::min(length[a][b], length[a][k] + length[k][b]); }
      }
    }
  }
  return length;
}

// Checks the closure's rows, as node numbers, against the reference.
void checkRows(const Graph& graph, const std::vector<std::pair<std::size_t, std::size_t>>& rows) {
  const auto length = walkLengths(graph);
  std::vector<std::pair<std::size_t, std::size_t>> expected;
  for (std::size_t a = 0; a < graph.nodes; ++a) {
    for (std::size_t b = 0; b < graph.nodes; ++b) {
      if (length[a][b] != kUnreached) { expected.emplace_back(a, b); }
    }
  }
  auto got = rows;
  std::ranges::sort(got);
  DREAMOR_CHECK(got == expected);    // every pair a walk joins, each once
  std::vector<std::size_t> sources;  // in the order the relation first lists each as a source
  for (const auto& [a, b] : graph.edges) {
    if (std::ranges::find(sources, a) == sources.end()) { sources.push_back(a); }
  }
  std::size_t at = 0;
  for (const std::size_t source : sources) {  // each source's rows together, in that order, nearest first
    std::size_t last = 0;
    for (; at < rows.size() && rows[at].first == source; ++at) {
      DREAMOR_CHECK(length[source][rows[at].second] >= last);
      last = length[source][rows[at].second];
    }
  }
  DREAMOR_CHECK(at == rows.size());
}

void overIntegers(const Graph& graph) {
  std::vector<std::tuple<cFrom, cTo>> edges;
  edges.reserve(graph.edges.size());
  for (const auto& [a, b] : graph.edges) {
    edges.emplace_back((static_cast<int>(a) * 7) - 50, (static_cast<int>(b) * 7) - 50);  // node numbers, relabelled
  }
  const auto sCLOSURE = dreamor::transitiveClosure(dreamor::index_set::relational::from(edges).build());
  std::vector<std::pair<std::size_t, std::size_t>> rows;
  for (const auto& [from, to] : sCLOSURE) {
    rows.emplace_back(static_cast<std::size_t>((from + 50) / 7), static_cast<std::size_t>((to + 50) / 7));
  }
  checkRows(graph, rows);
}

void overEntities(const Graph& graph) {
  dreamor::EntityVector<Node> nodes;
  for (std::size_t n = 0; n < graph.nodes; ++n) {
    static_cast<void>(nodes.emplace(Node{static_cast<int>(n)}));
  }
  nodes.freeze();
  std::vector<std::tuple<cSource, cTarget>> edges;
  edges.reserve(graph.edges.size());
  for (const auto& [a, b] : graph.edges) {
    edges.emplace_back(nodes.at(static_cast<int>(a)), nodes.at(static_cast<int>(b)));
  }
  const auto sCLOSURE = dreamor::transitiveClosure(dreamor::index_set::relational::from(edges).build());
  std::vector<std::pair<std::size_t, std::size_t>> rows;
  for (const auto& [from, to] : sCLOSURE) {
    rows.emplace_back(static_cast<std::size_t>(from->id), static_cast<std::size_t>(to->id));
  }
  checkRows(graph, rows);
}

}  // namespace

int main() try {
  constexpr std::uint32_t kSeed = 20261109;
  std::mt19937 g(kSeed);  // NOLINT(bugprone-random-generator-seed): fixed on purpose, every run checks the same cases
  for (std::size_t round = 0; round < 300; ++round) {
    const Graph graph = randomGraph(g, round % 2 == 1);
    overIntegers(graph);
    overEntities(graph);
  }
  return report("closure_differential");
} catch (...) { return ::uncaught(); }
