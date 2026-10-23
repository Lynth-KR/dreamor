#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Builds the visit-order studies: ./model, the planner over made-up sets (model.cpp); ./plan
# times each variant of the three-source experiment (plan.cpp), ./plan_count counts predicate
# calls instead. CXX picks the compiler (default g++).
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
FLAGS="-std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../../include"
$CXX $FLAGS model.cpp -o model
$CXX $FLAGS plan.cpp -o plan
$CXX $FLAGS -DPLAN_COUNT plan.cpp -o plan_count
echo "built bench/plan/model, plan and plan_count"
