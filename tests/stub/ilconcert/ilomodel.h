// SPDX-License-Identifier: Apache-2.0
// IloModel stand-in. Its own header, as in real Concert: <ilconcert/iloexpression.h> alone
// does not declare it, so a test that uses a model without including <ilcplex/ilocplex.h>
// (or <ilconcert/ilomodel.h>) fails here exactly as it would against the real headers.
#pragma once
#include <ilconcert/iloexpression.h>
#include <vector>

class IloObjective;
class IloModel {
 public:
  explicit IloModel(IloEnv) : cs_(IloStubArena::own(new std::vector<IloConstraintI*>)) {}
  IloEnv getEnv() const { return {}; }
  // const, as in real Concert: a model is a handle, and adding goes through it. It returns what it
  // added (an IloExtractable in real Concert), so `model.add(c).setName(...)` works as there.
  IloConstraint add(const IloConstraint& c) const {
    cs_->push_back(c.impl_);
    return c;
  }
  void add(const IloObjective&) const {}
  std::vector<IloConstraintI*>* cs_;
};
