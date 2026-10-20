// SPDX-License-Identifier: Apache-2.0
// IloCplex stand-in, ONLY so tests/cplex/*.cpp can be syntax-checked without CPLEX
// (run_tests.sh compiles them with -fsyntax-only in stub mode). It does not solve anything;
// the cplex tests run only with --cplex against the real headers and libraries.
#pragma once
#include <ilconcert/iloexpression.h>
#include <ilconcert/ilomodel.h>
#include <ostream>

class IloException {
 public:
  const char* getMessage() const { return ""; }
};
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
  bool solve() { return false; }
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
