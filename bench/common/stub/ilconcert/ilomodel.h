// SPDX-License-Identifier: Apache-2.0
// IloModel stand-in, in its own header as in real Concert. It keeps what is added to it, and counts
// rows and nonzeros as a solver would see them after extraction (a stand-in's own, for the checks).
#pragma once
#include <cstddef>
#include <vector>

#include <ilconcert/iloexpression.h>

class IloModel {
 public:
  explicit IloModel(IloEnv) : impl_(new Impl) {}
  // const, as in real Concert: a model is a handle, and adding goes through it.
  void add(const IloConstraint& c) const { impl_->constraints.push_back(c.getImpl()); }
  void add(const IloObjective& o) const { impl_->objective = o.getImpl(); }
  std::size_t rows() const { return impl_->constraints.size(); }
  std::size_t nonzeros() const {
    std::size_t n = 0;
    for (const IloConstraintI* c : impl_->constraints) {
      n += c->terms.vars.size();
    }
    return n;
  }
  const IloExprI* objective() const { return impl_->objective; }
  const std::vector<IloConstraintI*>& constraints() const { return impl_->constraints; }

 private:
  struct Impl {
    std::vector<IloConstraintI*> constraints;
    IloExprI* objective = nullptr;
  };
  Impl* impl_;
};
