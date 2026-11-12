// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A stand-in for IloModel, in a header of its own as in real Concert.
//
// It keeps what is added to it. For the checks, it counts the rows, and the terms of the constraints as the nonzeros.
// The counts are the stand-in's own, not a solver's.
#pragma once
#include <cstddef>
#include <vector>

#include <ilconcert/iloexpression.h>

class IloModel {
 public:
  explicit IloModel(IloEnv) : impl_(new Impl) {}

  // add is const, as in real Concert: a model is a handle, and an addition changes the object behind the handle.
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
