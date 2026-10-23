// SPDX-License-Identifier: Apache-2.0
// Concert stand-in for the container benchmark: no solver behind it, and as cheap as a
// handle-over-heap-object design can be, so the solver side is the same small constant in
// every arm (see bench/README.md). Not tests/stub: that one stores names and keeps every
// expression alive for the tests to inspect, which would put most of the memory and time
// on the solver side here.
//
// Only what dreamor/concert.hpp and bench.cpp use. A variable is a 16-byte heap object
// behind a handle, as in Concert. An expression records its variables and their
// coefficients, and end() frees it.
#pragma once
#include <cstddef>
#include <vector>

using IloNum = double;
using IloInt = long;

inline constexpr IloNum IloInfinity = 1e20;

class IloEnv {
 public:
  void end() {}
};

struct IloNumVarI {
  double lb, ub;
};

class IloNumVar {
 public:
  enum Type { Int, Float, Bool };
  IloNumVar() = default;
  IloNumVar(IloEnv, IloNum lb, IloNum ub, Type = Float, const char* = nullptr) : impl_(new IloNumVarI{lb, ub}) {}
  IloNumVarI* getImpl() const { return impl_; }
  IloNum getLB() const { return impl_->lb; }
  IloNum getUB() const { return impl_->ub; }

 private:
  IloNumVarI* impl_ = nullptr;
};
class IloSemiContVar : public IloNumVar {
 public:
  using IloNumVar::IloNumVar;
};
#define ILOINT IloNumVar::Int
#define ILOFLOAT IloNumVar::Float
#define ILOBOOL IloNumVar::Bool

class IloNumVarArray {
 public:
  explicit IloNumVarArray(IloEnv) : v_(new std::vector<IloNumVar>) {}
  void add(const IloNumVar& x) { v_->push_back(x); }
  IloInt getSize() const { return static_cast<IloInt>(v_->size()); }
  const IloNumVar& operator[](IloInt i) const { return (*v_)[static_cast<std::size_t>(i)]; }
  void end() {
    delete v_;
    v_ = nullptr;
  }

 private:
  std::vector<IloNumVar>* v_;
};
class IloNumArray {
 public:
  explicit IloNumArray(IloEnv) : v_(new std::vector<IloNum>) {}
  IloNum operator[](IloInt i) const { return (*v_)[static_cast<std::size_t>(i)]; }
  void end() {
    delete v_;
    v_ = nullptr;
  }

 private:
  std::vector<IloNum>* v_;
};

// coef * var, as Concert's IloNumExprArg holds it.
class IloNumExprArg {
 public:
  IloNumExprArg(IloNum coef, const IloNumVar& var) : coef_(coef), var_(var.getImpl()) {}
  IloNum coef() const { return coef_; }
  IloNumVarI* var() const { return var_; }

 private:
  IloNum coef_;
  IloNumVarI* var_;
};
inline IloNumExprArg operator*(IloNum coef, const IloNumVar& var) {
  return {coef, var};
}

struct IloExprI {
  std::vector<IloNumVarI*> vars;
  std::vector<IloNum> coefs;  // coefs[i] multiplies vars[i]
};
class IloExpr {
 public:
  explicit IloExpr(IloEnv) : impl_(new IloExprI) {}
  IloExpr& operator+=(const IloNumVar& v) {
    impl_->vars.push_back(v.getImpl());
    impl_->coefs.push_back(1);
    return *this;
  }
  IloExpr& operator+=(const IloNumExprArg& term) {
    impl_->vars.push_back(term.var());
    impl_->coefs.push_back(term.coef());
    return *this;
  }
  IloExprI* getImpl() const { return impl_; }
  void end() {
    delete impl_;
    impl_ = nullptr;
  }

 private:
  IloExprI* impl_;
};
