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
// ortools/math_opt/samples/cpp/time_indexed_scheduling.cc at
// google/or-tools@100f66e; examples/mathopt_samples/README.md walks through
// what changed. Logging goes through dreamor/logger/logger_light.hpp.

// Problem statement.
//
// Data:
//  * n jobs
//  * processing time p_i for i = 1,...,n
//  * release time r_i for i = 1,...,n
//  * Implied: T = max_i r_i + sum_i p_i, the time horizon, all jobs must start
//    in [0, T].
//
// Problem: schedule the jobs sequentially (on a single machine) to minimize the
// sum of the completion times, where each job cannot start until the release
// time. In the scheduling literature, this problem is 1|r_i|sum_i C_i. This
// problem is known to be NP-Hard (e.g. see "Elements of Scheduling" by Lenstra
// and Shmoys 2020, Chapter 4).
//
// Variables:
//  * x_it for job i = 1,...,n and time t = 1,...,T, if job i starts at time t.
//
// Model:
//   min   sum_i sum_t (t + p_i) * x_it
//   s.t.  sum_t x_it = 1                     for all i = 1,...,n     (1)
//         sum_i sum_{s=t-p_i+1}^t x_is <= 1  for all t = 0,...,T     (2)
//         x_it = 0                           for all i, for t < r_i  (3)
//         x_it in {0, 1}                     for all i and t
//
// In the objective, t + p_i is the time the job is completed if it starts at t.
// Constraint (1) ensures that each job is scheduled once, constraint (2)
// ensures that no two jobs overlap in when they are running, and constraint (3)
// enforces the release dates.
//
// In this port, (3) is not a constraint but the index set: x_it exists only for
// t >= r_i. And (2) is stated only for the times some job can be running - for
// any other t its sum is empty and the constraint says nothing.

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "ortools/base/init_google.h"
#include "ortools/base/status_builder.h"
#include "ortools/base/status_macros.h"
#include "ortools/math_opt/cpp/math_opt.h"
#include "dreamor/index_set.hpp"
#include "dreamor/logger/logger_light.hpp"
#include "dreamor/mathopt.hpp"

ABSL_FLAG(operations_research::math_opt::SolverType, solver_type,
          operations_research::math_opt::SolverType::kGscip,
          "The solver needs to support binary IP.");

ABSL_FLAG(int, num_jobs, 30, "How many jobs to schedule");
ABSL_FLAG(bool, use_test_data, false,
          "Solve a small hard coded instance instead of a large random one.");

namespace {

namespace math_opt = operations_research::math_opt;

struct Job {
  int processing_time = 0;
  int release_time = 0;
};

std::vector<Job> RandomJobs(int num_jobs) {
  // Processing times are uniform [1, kProcessingTimeUb].
  constexpr int kProcessingTimeUb = 20;

  // Release times are uniform in [0, kReleaseTimeUb].
  const int kReleaseTimeUb = num_jobs * kProcessingTimeUb / 2;

  absl::BitGen bit_gen;
  std::vector<Job> result;
  result.reserve(num_jobs);
  for (int i = 0; i < num_jobs; ++i) {
    result.push_back(
        {.processing_time = absl::Uniform(bit_gen, 1, kProcessingTimeUb),
         .release_time = absl::Uniform(bit_gen, 0, kReleaseTimeUb)});
  }
  return result;
}

// A small instance for testing. The optimal solution is to run:
//   Job 1 at time 1
//   Job 2 at time 2
//   Job 0 at time 7
// This gives a sum of completion times of 2 + 7 + 17 = 26
//
// Note that the above schedule idles at time 0. If instead, we did
//   Job 2 at time 0
//   Job 1 at time 5
//   Job 0 at time 6
// This gives a sum of completion times of 5 + 6 + 16 = 27
std::vector<Job> TestInstance() {
  return {{.processing_time = 10, .release_time = 0},
          {.processing_time = 1, .release_time = 1},
          {.processing_time = 5, .release_time = 0}};
}

int TimeHorizon(absl::Span<const Job> jobs) {
  int max_release = 0;
  int sum_processing = 0;
  for (const Job& job : jobs) {
    max_release = std::max(job.release_time, max_release);
    sum_processing += job.processing_time;
  }
  return max_release + sum_processing;
}

struct Schedule {
  std::vector<int> start_times;
  int sum_of_completion_times = 0;
};

// A job is its index in `jobs`; times are integers.
using JobCol = dreamor::Col<int, "job">;
using StartCol = dreamor::Col<int, "start">;
using TimeCol = dreamor::Col<int, "time">;

absl::StatusOr<Schedule> Solve(absl::Span<const Job> jobs,
                               const math_opt::SolverType solver_type) {
  const int kTimeHorizon = TimeHorizon(jobs);
  const auto job_ids = std::views::iota(0, static_cast<int>(jobs.size()));

  // set JOBS;
  // set START := {j in JOBS, s in release[j]..T-1};                        (3)
  // set RUNNING := {(j,s) in START, t in s..min(s+p[j], T)-1};
  const auto starts_of = [&](JobCol j) {
    return std::views::iota(jobs[j].release_time, kTimeHorizon);
  };
  const auto running_at = [&](JobCol j, StartCol s) {
    return std::views::iota(s.get(), std::min(s + jobs[j].processing_time, kTimeHorizon));
  };
  const auto sJOBS = dreamor::makeSet<JobCol>(job_ids).collect();
  const auto sSTART = dreamor::makeSet<JobCol, StartCol>(job_ids, starts_of).collect();
  const auto sRUNNING = dreamor::makeSet<TimeCol>(sSTART, running_at).collect();

  math_opt::Model model;
  // var x {START} binary;  - x[j,s] = 1 if job j starts at time s
  const dreamor::mathopt::VarTable<JobCol, StartCol> vStart{
      model, sSTART, 0, 1, /*is_integer=*/true, "x"};

  // minimize SumCompletion: sum {(j,s) in START} (s + p[j]) * x[j,s];
  const auto completion_time = [&](JobCol j, StartCol s) {
    return (s + jobs[j].processing_time) * vStart(j, s);
  };
  const math_opt::LinearExpression sum_completion_times =
      dreamor::mathopt::sumOf(sSTART, completion_time).build();
  model.Minimize(sum_completion_times);

  // subject to Once {j in JOBS}: sum {(j,s) in START} x[j,s] = 1;            (1)
  // Over JOBS, not over START's groups: a job with no start left would get no
  // row from groupBy and silently vanish from the model; here it gets 0 = 1.
  const auto startsByJob = sSTART.groupBy<JobCol>();
  for (const auto& [j] : sJOBS) {
    model.AddLinearConstraint(dreamor::mathopt::sumOf(startsByJob.slice(j), vStart).build() == 1.0);
  }
  // subject to OneAtATime {t in TIMES}: sum {(j,s,t) in RUNNING} x[j,s] <= 1;  (2)
  for (const auto& [t, rows] : sRUNNING.groupBy<TimeCol>()) {
    model.AddLinearConstraint(dreamor::mathopt::sumOf(rows, vStart).build() <= 1.0);
  }

  ASSIGN_OR_RETURN(const math_opt::SolveResult result,
                   math_opt::Solve(model, solver_type,
                                   {.parameters = {.enable_output = true}}));
  if (!result.has_primal_feasible_solution()) {
    return util::InvalidArgumentErrorBuilder()
           << "no primal feasible solution, termination: "
           << result.termination;
  }
  Schedule schedule;
  // Rounded, not truncated: the solver's binaries are only near 0 and 1, and
  // assigning 2893.9999999 to an int would report 2893.
  schedule.sum_of_completion_times = static_cast<int>(
      std::lround(sum_completion_times.Evaluate(result.variable_values())));
  schedule.start_times.resize(jobs.size());
  for (const auto& [row, started] : vStart.solution(result)) {
    if (started > 0.5) {
      const auto& [j, s] = row;
      schedule.start_times[j] = s;
    }
  }
  return schedule;
}

void PrintSchedule(absl::Span<const Job> jobs, const Schedule& schedule) {
  DREAMOR_LOG_INFO("sum of completion times: {}", schedule.sum_of_completion_times);
  std::vector<std::pair<int, Job>> jobs_by_start_time;
  for (int i = 0; i < jobs.size(); ++i) {
    jobs_by_start_time.push_back({schedule.start_times[i], jobs[i]});
  }
  absl::c_sort(jobs_by_start_time, [](const auto& left, const auto& right) {
    return left.first < right.first;
  });
  DREAMOR_LOG_INFO("start time, processing time, release time");
  for (const auto [start_time, job] : jobs_by_start_time) {
    DREAMOR_LOG_INFO("{}, {}, {}", start_time, job.processing_time, job.release_time);
  }
}

absl::Status Main() {
  std::vector<Job> jobs;
  if (absl::GetFlag(FLAGS_use_test_data)) {
    jobs = TestInstance();
  } else {
    const int num_jobs = absl::GetFlag(FLAGS_num_jobs);
    jobs = RandomJobs(num_jobs);
  }
  ASSIGN_OR_RETURN(const Schedule schedule,
                   Solve(jobs, absl::GetFlag(FLAGS_solver_type)));
  PrintSchedule(jobs, schedule);
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
