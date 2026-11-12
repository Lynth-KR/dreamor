// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// IloCplex stand-in, ONLY so tests/cplex/*.cpp can be syntax-checked without CPLEX
// (the syntax tests of tests/CMakeLists.txt). It does not solve anything; the cplex tests run
// only with DREAMOR_CPLEX_DIR, against the real headers and libraries.
#pragma once
#include <ilconcert/iloexpression.h>
#include <ilconcert/ilomodel.h>
#include <ostream>

class IloAlgorithm {
 public:
  enum Status { Unknown, Feasible, Optimal, Infeasible, Unbounded, InfeasibleOrUnbounded, Error };
};

class IloObjective : public IloNumExprArg {};

inline IloObjective IloMaximize(IloEnv, const IloNumExprArg&) {
  return {};
}

inline IloObjective IloMinimize(IloEnv, const IloNumExprArg&) {
  return {};
}

class IloCplex {
 public:
  explicit IloCplex(IloModel) {}

  IloEnv getEnv() const { return {}; }

  void setOut(std::ostream&) {}

  IloBool solve() { return IloFalse; }

  IloAlgorithm::Status getStatus() const { return IloAlgorithm::Unknown; }

  IloNum getObjValue() const { return 0; }

  IloNum getValue(const IloNumVar&) const { return 0; }

  void getValues(IloNumArray, const IloNumVarArray) const {}

  IloInt getNrows() const { return 0; }

  IloInt getNcols() const { return 0; }

  IloInt getNNZs() const { return 0; }

  void exportModel(const char*) const {}

  void end() {}
};
