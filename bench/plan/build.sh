#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
# not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Builds the visit-order studies:
#   ./model       runs the planner over made-up sets, from model.cpp.
#   ./plan        times each variant of the three-source experiment, from plan.cpp.
#   ./plan_count  counts the predicate calls of each variant instead of timing it.
# CXX picks the compiler. The default is g++.
set -e
cd "$(dirname "$0")"
CXX=${CXX:-g++}
FLAGS="-std=c++20 -O2 -DNDEBUG -march=native -Wall -Wextra -I../../include"
$CXX $FLAGS model.cpp -o model
$CXX $FLAGS plan.cpp -o plan
$CXX $FLAGS -DPLAN_COUNT plan.cpp -o plan_count
echo "built bench/plan/model, plan and plan_count"
