// SPDX-License-Identifier: Apache-2.0
// Copyright 2010-2025 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Modified by Lynth-KR, 2026: ported to dreamor. The original is
// ortools/math_opt/samples/cpp/lagrangian_relaxation.cc at
// google/or-tools@100f66e; examples/mathopt_samples/README.md walks through
// what changed. Logging goes through dreamor/logger/logger_light.hpp.

// Solves a constrained shortest path problem via Lagrangian Relaxation. The
// Lagrangian dual is solved with subgradient ascent.
//
// Problem data:
// * N: set of nodes.
// * A: set of arcs.
// * R: set of resources.
// * c_(i,j): cost of traversing arc (i,j) in A.
// * r_(i,j,k): resource k spent by traversing arc (i,j) in A, for all k in R.
// * b_i: flow balance at node i in N (+1 at the source, -1 at the sink, and 0
//        otherwise).
// * r_max_k: availability of resource k for a path, for all k in R.
//
// Decision variables:
// * x_(i,j): flow through arc (i,j) in A.
//
// Formulation:
// Z = min  sum(c_(i,j) * x_(i,j): (i,j) in A)
//     s.t.
//     sum(x_(i,j): (i,j) in A) - sum(x_(j,i): (j,i) in A) = b_i for all i in N,
//     sum(r_(i,j,k) * x_(i,j): (i,j) in A) <= r_max_k for all k in R,
//     x_(i,j) in {0,1} for all (i,j) in A.
//
// Upon dualizing a subset of the constraints (here we chose to relax some or
// all of the knapsack constraints), we obtaing a subproblem parameterized by
// dual variables mu (one per dualized constraint). We refer to this as the
// Lagrangian subproblem. Let R+ be the set of knapsack constraints that we
// keep, and R- the set of knapsack constraints that get dualized. The
// Lagrangian subproblem follows:
//
// z(mu) = min  sum(
//              (c_(i,j) - sum(mu_k * r_(i,j,k): k in R)) * x_(i,j): (i,j) in A)
//              + sum(mu_k * r_max_k: k in R-)
// s.t.
//   sum(x_(i,j): (i,j) in A) - sum(x_(j,i): (j,i) in A) = b_i for all i in N,
// sum(r_(i,j,k) * x_(i,j): (i,j) in A) <= r_max_k for all k in R+,
//   x_(i,j) in {0,1} for all (i,j) in A.
//
// We seek to solve the Lagrangian dual, which is of the form:
// Z_D = max{ z(mu) : mu <=0 }. Concavity of z(mu) allows us to solve the
// Lagrangian dual with the iterates:
// mu_(t+1) = mu_t + step_size_t * grad_mu_t, where
// grad_mu_t = r_max - sum(t_(i,j) * x_(i,j)^t: (i,j) in A) is a subgradient of
// z(mu_t) and x^t is an optimal solution to the problem induced by z(mu_t).
//
// In general we have that Z_D <= Z. For convex problems, Z_D = Z. For MIPs,
// Z_LP <= Z_D <= Z, where Z_LP is the linear relaxation of the original
// problem.
//
// In this particular example, we use two resource constraints. Either
// constraint or both can be dualized via the flags `dualize_resource_1` and
// `dualize_resource_2`. If both constraints are dualized, we have that Z_LP =
// Z_D because the resulting Lagrangian subproblem can be solved as a linear
// program (i.e., the problem becomes a pure shortest path problem upon
// dualizing all the side constraints). When only one of the side constraints is
// dualized, we can have Z_LP <= Z_D because the resulting Lagrangian subproblem
// needs to be solved as an MIP. For the particular data used in this example,
// dualizing only the first resource constraint leads to Z_LP < Z_D, while
// dualizing only the second resource constraint leads to Z_LP = Z_D. In either
// case, solving the Lagrandual dual also provides an upper bound to Z.
//
// Usage: blaze build -c opt
// ortools/math_opt/examples:lagrangian_relaxation
// blaze-bin/ortools/math_opt/examples/lagrangian_relaxation

#include <math.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <ranges>
#include <sstream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "ortools/base/container_logging.h"
#include "ortools/base/init_google.h"
#include "ortools/base/logging.h"
#include "ortools/base/mathutil.h"
#include "ortools/base/status_builder.h"
#include "ortools/base/status_macros.h"
#include "ortools/math_opt/cpp/math_opt.h"
#include "dreamor/index_set.hpp"
#include "dreamor/logger/logger_light.hpp"
#include "dreamor/mathopt.hpp"

ABSL_FLAG(double, step_size, 0.95,
          "Stepsize for gradient ascent, determined as step_size^t.");
ABSL_FLAG(int, max_iterations, 1000,
          "Max number of iterations for gradient ascent.");
ABSL_FLAG(bool, dualize_resource_1, true,
          "If true, the side constraint associated to resource 1 is dualized.");
ABSL_FLAG(bool, dualize_resource_2, false,
          "If true, the side constraint associated to resource 2 is dualized.");

ABSL_FLAG(bool, lagrangian_output, false,
          "If true, shows the iteration log of the subgradient ascent "
          "procedure use to solve the Lagrangian problem");

constexpr double kZeroTol = 1.0e-8;

namespace {
using ::operations_research::MathUtil;

namespace math_opt = ::operations_research::math_opt;

struct Arc {
  int i;
  int j;
  double cost;
  double resource_1;
  double resource_2;
};

struct Graph {
  int num_nodes;
  std::vector<Arc> arcs;
  int source;
  int sink;
};

// An arc is an entity of the graph; its tail and head are values read off it.
using ArcCol = dreamor::Col<const Arc*, "arc">;
using NodeCol = dreamor::Col<int, "node">;
using TailCol = dreamor::Col<int, "tail">;
using HeadCol = dreamor::Col<int, "head">;

// The token an arc contributes to its variable's name: x_<i>_<j>.
inline std::string keyName(const Arc* arc) {
  return absl::StrFormat("%d_%d", arc->i, arc->j);
}

// set NODES := 0..num_nodes-1;
// set ARCS;
// set ARC_ENDS := {a in ARCS, t in {tail[a]}, h in {head[a]}};
//
// The sets point into graph.arcs, so the graph must outlive them (and every
// FlowModel built on it).
struct Network {
  explicit Network(const Graph& graph) {
    const auto node_ids = std::views::iota(0, graph.num_nodes);
    const auto tail_of = [](ArcCol a) { return std::views::single(a->i); };
    const auto head_of = [](ArcCol a) { return std::views::single(a->j); };
    sNODES = dreamor::makeSet<NodeCol>(node_ids).collect();
    sARC_ENDS = dreamor::makeSet<ArcCol, TailCol, HeadCol>(graph.arcs, tail_of,
                                                          head_of)
                    .collect();
    sARCS = sARC_ENDS.select<ArcCol>();
  }
  dreamor::SetOf<NodeCol> sNODES;
  dreamor::SetOf<ArcCol, TailCol, HeadCol> sARC_ENDS;
  dreamor::SetOf<ArcCol> sARCS;
};

// The shortest path model: flow variables, the cost and resource expressions,
// and the flow balance constraints. The sets live in the base class so that
// they exist before the VarTable that is built on them.
struct FlowModel : Network {
  FlowModel(const Graph& graph, bool integer);
  std::unique_ptr<math_opt::Model> model;
  dreamor::mathopt::VarTable<ArcCol> vFlow;  // var x {ARCS} >= 0, <= 1;
  math_opt::LinearExpression cost;
  math_opt::LinearExpression resource_1;
  math_opt::LinearExpression resource_2;
};

FlowModel::FlowModel(const Graph& graph, const bool integer)
    : Network(graph),
      model(std::make_unique<math_opt::Model>("LagrangianProblem")),
      vFlow(*model, sARCS, 0, 1, integer, "x"),
      // sum {a in ARCS} cost[a] * x[a], and the same for each resource
      cost(dreamor::mathopt::sumOf(sARCS, [this](ArcCol a) {
             return a->cost * vFlow(a);
           }).build()),
      resource_1(dreamor::mathopt::sumOf(sARCS, [this](ArcCol a) {
                   return a->resource_1 * vFlow(a);
                 }).build()),
      resource_2(dreamor::mathopt::sumOf(sARCS, [this](ArcCol a) {
                   return a->resource_2 * vFlow(a);
                 }).build()) {
  // subject to Balance {n in NODES}:
  //   sum {(a,n,h) in ARC_ENDS} x[a] - sum {(a,t,n) in ARC_ENDS} x[a] = b[n];
  // Over NODES, not over the arcs' groups: a node with no arc out (the sink)
  // or no arc in (the source) still gets its row.
  const auto outOf = sARC_ENDS.groupBy<TailCol>();
  const auto into = sARC_ENDS.groupBy<HeadCol>();
  for (const auto& [n] : sNODES) {
    const int rhs = n.get() == graph.source ? 1
                    : n.get() == graph.sink ? -1
                                            : 0;
    model->AddLinearConstraint(
        dreamor::mathopt::sumOf(outOf.slice(TailCol{n.get()}), vFlow).build() -
            dreamor::mathopt::sumOf(into.slice(HeadCol{n.get()}), vFlow).build() ==
        rhs);
  }
}

// Populates `model` with variables and constraints of a shortest path problem.
FlowModel CreateShortestPathModel(const Graph& graph) {
  return FlowModel(graph, /*integer=*/false);
}

Graph CreateSampleNetwork() {
  std::vector<Arc> arcs;
  arcs.push_back(
      {.i = 0, .j = 1, .cost = 12, .resource_1 = 1, .resource_2 = 1});
  arcs.push_back(
      {.i = 0, .j = 2, .cost = 3, .resource_1 = 2.5, .resource_2 = 1});
  arcs.push_back(
      {.i = 1, .j = 3, .cost = 5, .resource_1 = 1, .resource_2 = 1.5});
  arcs.push_back(
      {.i = 1, .j = 4, .cost = 5, .resource_1 = 2.5, .resource_2 = 1});
  arcs.push_back(
      {.i = 2, .j = 1, .cost = 7, .resource_1 = 2.5, .resource_2 = 1});
  arcs.push_back(
      {.i = 2, .j = 3, .cost = 5, .resource_1 = 7, .resource_2 = 2.5});
  arcs.push_back(
      {.i = 2, .j = 4, .cost = 1, .resource_1 = 6.5, .resource_2 = 1});
  arcs.push_back(
      {.i = 3, .j = 5, .cost = 6, .resource_1 = 1, .resource_2 = 2.0});
  arcs.push_back(
      {.i = 4, .j = 3, .cost = 3, .resource_1 = 1, .resource_2 = 0.5});
  arcs.push_back(
      {.i = 4, .j = 5, .cost = 5, .resource_1 = 2.5, .resource_2 = 1});
  const Graph graph = {.num_nodes = 6, .arcs = arcs, .source = 0, .sink = 5};

  return graph;
}

// Solves the constrained shortest path as an MIP.
absl::StatusOr<FlowModel> SolveMip(const Graph& graph,
                                   const double max_resource_1,
                                   const double max_resource_2) {
  FlowModel flow_model(graph, /*integer=*/true);
  math_opt::Model& model = *flow_model.model;
  model.AddLinearConstraint(flow_model.resource_1 <= max_resource_1,
                            "resource_ctr_1");
  model.AddLinearConstraint(flow_model.resource_2 <= max_resource_2,
                            "resource_ctr_2");
  model.Minimize(flow_model.cost);
  ASSIGN_OR_RETURN(const math_opt::SolveResult result,
                   Solve(model, math_opt::SolverType::kGscip));
  RETURN_IF_ERROR(result.termination.EnsureIsOptimalOrFeasible());
  DREAMOR_LOG_INFO("MIP Solution with 2 side constraints");
  DREAMOR_LOG_INFO("MIP objective value: {:6.3f}", result.objective_value());
  DREAMOR_LOG_INFO("Resource 1: {:g}",
                   flow_model.resource_1.Evaluate(result.variable_values()));
  DREAMOR_LOG_INFO("Resource 2: {:g}",
                   flow_model.resource_2.Evaluate(result.variable_values()));
  DREAMOR_LOG_INFO("========================================");
  return flow_model;
}

// Solves the linear relaxation of a constrained shortest path problem
// formulated as an MIP.
absl::Status SolveLinearRelaxation(FlowModel& flow_model, const Graph& graph,
                                   const double max_resource_1,
                                   const double max_resource_2) {
  math_opt::Model& model = *flow_model.model;
  ASSIGN_OR_RETURN(const math_opt::SolveResult result,
                   Solve(model, math_opt::SolverType::kGscip));
  RETURN_IF_ERROR(result.termination.EnsureIsOptimalOrFeasible());
  DREAMOR_LOG_INFO("LP relaxation with 2 side constraints");
  DREAMOR_LOG_INFO("LP objective value: {:6.3f}", result.objective_value());
  DREAMOR_LOG_INFO("Resource 1: {:g}",
                   flow_model.resource_1.Evaluate(result.variable_values()));
  DREAMOR_LOG_INFO("Resource 2: {:g}",
                   flow_model.resource_2.Evaluate(result.variable_values()));
  DREAMOR_LOG_INFO("========================================");
  return absl::OkStatus();
}

absl::Status SolveLagrangianRelaxation(const Graph& graph,
                                       const double max_resource_1,
                                       const double max_resource_2) {
  // Model, variables, and linear expressions.
  FlowModel flow_model = CreateShortestPathModel(graph);
  math_opt::Model& model = *flow_model.model;
  math_opt::LinearExpression& cost = flow_model.cost;
  math_opt::LinearExpression& resource_1 = flow_model.resource_1;
  math_opt::LinearExpression& resource_2 = flow_model.resource_2;

  // Dualized constraints and dual variable iterates.
  std::vector<double> mu;
  std::vector<math_opt::LinearExpression> grad_mu;
  const bool dualized_resource_1 = absl::GetFlag(FLAGS_dualize_resource_1);
  const bool dualized_resource_2 = absl::GetFlag(FLAGS_dualize_resource_2);
  const bool lagrangian_output = absl::GetFlag(FLAGS_lagrangian_output);
  CHECK(dualized_resource_1 || dualized_resource_2)
      << "At least one of the side constraints should be dualized.";

  // Modify the lagrangian problem according to the constraints that are
  // dualized. We use a initial dual value different from zero to prioritize
  // finding a feasible solution.
  const double initial_dual_value = -10;
  if (dualized_resource_1 && !dualized_resource_2) {
    mu.push_back(initial_dual_value);
    grad_mu.push_back(max_resource_1 - resource_1);
    model.AddLinearConstraint(resource_2 <= max_resource_2);
    for (const auto& [arc, var] : flow_model.vFlow) {
      model.set_integer(var);
    }
  } else if (!dualized_resource_1 && dualized_resource_2) {
    mu.push_back(initial_dual_value);
    grad_mu.push_back(max_resource_2 - resource_2);
    model.AddLinearConstraint(resource_1 <= max_resource_1);
    for (const auto& [arc, var] : flow_model.vFlow) {
      model.set_integer(var);
    }
  } else {
    mu.push_back(initial_dual_value);
    mu.push_back(initial_dual_value);
    grad_mu.push_back(max_resource_1 - resource_1);
    grad_mu.push_back(max_resource_2 - resource_2);
  }

  // Gradient ascent setup
  bool termination = false;
  int iterations = 1;
  const double step_size = absl::GetFlag(FLAGS_step_size);
  CHECK_GT(step_size, 0) << "step_size must be strictly positive";
  CHECK_LT(step_size, 1) << "step_size must be strictly less than 1";
  const int max_iterations = absl::GetFlag(FLAGS_max_iterations);
  CHECK_GT(max_iterations, 0)
      << "Number of iterations must be strictly positive.";

  // Upper and lower bounds on the full problem.
  double upper_bound = std::numeric_limits<double>::infinity();
  double lower_bound = -std::numeric_limits<double>::infinity();
  double best_solution_resource_1 = 0;
  double best_solution_resource_2 = 0;

  if (lagrangian_output) {
    DREAMOR_LOG_INFO("Starting gradient ascent...");
    DREAMOR_LOG_INFO("{:>4} {:>6} {:>6} {:>9} {:>10} {:>10}", "Iter", "LB", "UB",
                     "Step size", "mu_t", "grad_mu_t");
  }

  while (!termination) {
    math_opt::LinearExpression lagrangian_function;
    lagrangian_function += cost;
    for (int k = 0; k < mu.size(); ++k) {
      lagrangian_function += mu[k] * grad_mu[k];
    }
    model.Minimize(lagrangian_function);
    ASSIGN_OR_RETURN(math_opt::SolveResult result,
                     Solve(model, math_opt::SolverType::kGscip));
    RETURN_IF_ERROR(result.termination.EnsureIsOptimalOrFeasible());

    const math_opt::VariableMap<double>& vars_val = result.variable_values();
    bool feasible = true;

    // Iterate update. Takes a step in the direction of the gradient (since the
    // Lagrangian dual is a max problem), and projects onto {mu: mu <=0} to
    // satisfy the sign of the dual variable. In general, convergence to an
    // optimal solution requires diminishing step sizes satisfying:
    //       * sum(step_size_t: t=1...) = infinity and,
    //       * sum((step_size_t)^2: t=1...) < infinity
    // See details in Prop 3.2.6 Bertsekas 2015, Convex Optimization Algorithms.
    // Here we use step_size_t = step_size^t which does NOT satisfy the
    // first condition, but is good enough for the purpose of this example.
    std::vector<double> grad_mu_vals;
    const double step_size_t = MathUtil::IPow(step_size, iterations);
    for (int k = 0; k < mu.size(); ++k) {
      // Evaluate resource k and evaluate the gradient of z(mu).
      const double grad_mu_val = grad_mu[k].Evaluate(vars_val);
      grad_mu_vals.push_back(grad_mu_val);
      mu[k] = std::min(0.0, mu[k] + step_size_t * grad_mu_val);
      if (grad_mu_val < 0) {
        feasible = false;
      }
    }
    // Bounds update
    const double path_cost = cost.Evaluate(vars_val);
    if (feasible && path_cost < upper_bound) {
      best_solution_resource_1 = resource_1.Evaluate(vars_val);
      best_solution_resource_2 = resource_2.Evaluate(vars_val);
      if (lagrangian_output) {
        DREAMOR_LOG_INFO(
            "Feasible solution with cost={:4.2f}, resource_1={:4.2f}, and "
            "resource_2={:4.2f}. ",
            path_cost, best_solution_resource_1, best_solution_resource_2);
      }
      upper_bound = path_cost;
    }
    if (lower_bound < result.objective_value()) {
      lower_bound = result.objective_value();
    }

    if (lagrangian_output) {
      std::ostringstream duals;
      duals << gtl::LogContainer(mu) << " " << gtl::LogContainer(grad_mu_vals);
      DREAMOR_LOG_INFO("{:4d} {:6.3f} {:6.3f} {:9.3f} {}", iterations,
                       lower_bound, upper_bound, step_size_t, duals.str());
    }

    // Termination criteria
    double norm = 0;
    for (double value : grad_mu_vals) {
      norm += (value * value);
    }
    norm = sqrt(norm);
    if (iterations == max_iterations || lower_bound == upper_bound ||
        step_size_t * norm < kZeroTol) {
      termination = true;
    }
    iterations++;
  }

  DREAMOR_LOG_INFO("Lagrangian relaxation with 2 side constraints");
  DREAMOR_LOG_INFO("Constraint for resource 1 dualized: {}", dualized_resource_1);
  DREAMOR_LOG_INFO("Constraint for resource 2 dualized: {}", dualized_resource_2);
  DREAMOR_LOG_INFO("Lower bound: {:6.3f}", lower_bound);
  DREAMOR_LOG_INFO("Upper bound: {:6.3f} (Integer solution)", upper_bound);
  DREAMOR_LOG_INFO("========================================");
  return absl::OkStatus();
}

void RelaxModel(FlowModel& flow_model) {
  for (const auto& [arc, var] : flow_model.vFlow) {
    flow_model.model->set_continuous(var);
    flow_model.model->set_lower_bound(var, 0.0);
    flow_model.model->set_upper_bound(var, 1.0);
  }
}

absl::Status SolveFullModel(const Graph& graph, double max_resource_1,
                            double max_resource_2) {
  ASSIGN_OR_RETURN(FlowModel flow_model,
                   SolveMip(graph, max_resource_1, max_resource_2));
  RelaxModel(flow_model);
  return SolveLinearRelaxation(flow_model, graph, max_resource_1,
                               max_resource_2);
}

absl::Status Main() {
  // Problem data
  const Graph graph = CreateSampleNetwork();
  const double max_resource_1 = 10;
  const double max_resource_2 = 4;

  RETURN_IF_ERROR(SolveFullModel(graph, max_resource_1, max_resource_2))
      << "full solve failed";
  RETURN_IF_ERROR(
      SolveLagrangianRelaxation(graph, max_resource_1, max_resource_2))
      << "lagrangian solve failed";
  return absl::OkStatus();
}
}  // namespace

int main(int argc, char** argv) {
  InitGoogle(argv[0], &argc, &argv, true);
  const absl::Status status = Main();
  if (!status.ok()) {
    DREAMOR_LOG_FATAL("{}", status.ToString());
  }
  return 0;
}
