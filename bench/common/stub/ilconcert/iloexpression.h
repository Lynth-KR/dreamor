// SPDX-License-Identifier: Apache-2.0
// Concert stand-in for the container benchmark: no solver behind it, and as cheap as a
// handle-over-heap-object design can be, so the solver side is the same small constant in
// every arm (bench/README.md). Not tests/stub: that one stores names and keeps every
// expression alive for the tests to inspect, which would put most of the memory and time
// on the solver side here.
//
// Only what dreamor/concert.hpp and the benchmarks use. A variable is a 16-byte heap object
// behind a handle, as in Concert. An expression records its variables and their
// coefficients, and end() frees it. A constraint or an objective keeps a copy of its terms,
// so the expression it came from can be ended, as in Concert.
#pragma once
#include <cstddef>
#include <utility>
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
  void add(IloNum x) { v_->push_back(x); }
  IloInt getSize() const { return static_cast<IloInt>(v_->size()); }
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
  IloNum constant = 0;
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

// A constraint, lb <= terms <= ub, with its own copy of the terms.
struct IloConstraintI {
  IloExprI terms;
  IloNum lb, ub;
};
class IloConstraint {
 public:
  explicit IloConstraint(IloConstraintI* impl) : impl_(impl) {}
  IloConstraintI* getImpl() const { return impl_; }

 private:
  IloConstraintI* impl_;
};
using IloRange = IloConstraint;  // Concert's comparison operators give an IloRange
inline IloConstraint operator<=(const IloNumVar& a, const IloNumVar& b) {
  return IloConstraint{new IloConstraintI{{{a.getImpl(), b.getImpl()}, {1, -1}}, -IloInfinity, 0}};
}
inline IloConstraint operator<=(const IloNumVar& a, const IloNumExprArg& b) {
  return IloConstraint{new IloConstraintI{{{a.getImpl(), b.var()}, {1, -b.coef()}}, -IloInfinity, 0}};
}
inline IloConstraint operator==(const IloExpr& e, IloNum rhs) {
  const IloExprI& terms = *e.getImpl();
  return IloConstraint{new IloConstraintI{terms, rhs - terms.constant, rhs - terms.constant}};
}
inline IloConstraint operator<=(const IloExpr& e, IloNum rhs) {
  const IloExprI& terms = *e.getImpl();
  return IloConstraint{new IloConstraintI{terms, -IloInfinity, rhs - terms.constant}};
}
inline IloConstraint operator==(const IloExpr& e, const IloNumVar& v) {
  auto* c = new IloConstraintI{*e.getImpl(), 0, 0};
  c->terms.vars.push_back(v.getImpl());
  c->terms.coefs.push_back(-1);
  c->lb = c->ub = -c->terms.constant;
  return IloConstraint{c};
}
// A new expression, as Concert's arithmetic on expressions makes one.
inline IloExpr operator-(const IloExpr& e, IloNum c) {
  IloExpr out(IloEnv{});
  *out.getImpl() = *e.getImpl();
  out.getImpl()->constant -= c;
  return out;
}
inline IloExpr operator+(const IloExpr& a, const IloExpr& b) {
  IloExpr out(IloEnv{});
  IloExprI& sum = *out.getImpl();
  sum           = *a.getImpl();
  sum.vars.insert(sum.vars.end(), b.getImpl()->vars.begin(), b.getImpl()->vars.end());
  sum.coefs.insert(sum.coefs.end(), b.getImpl()->coefs.begin(), b.getImpl()->coefs.end());
  sum.constant += b.getImpl()->constant;
  return out;
}

class IloObjective {
 public:
  explicit IloObjective(IloExprI* impl) : impl_(impl) {}
  IloExprI* getImpl() const { return impl_; }

 private:
  IloExprI* impl_;
};
inline IloObjective IloMinimize(IloEnv, const IloExpr& e) {
  return IloObjective{new IloExprI{*e.getImpl()}};
}

// IloModel is not here: as in real Concert, it lives in <ilconcert/ilomodel.h>.
