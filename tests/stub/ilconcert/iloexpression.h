// SPDX-License-Identifier: Apache-2.0
// Concert stub for the opt tests - no CPLEX needed. Mirrors the parts of Concert's shape
// the library and the examples rely on: handle classes over heap implementations,
// IloNumVar and IloExpr both IloNumExprArg, operators on IloNumExprArg, arrays with end().
// Constraints record (variable, coefficient) terms so tests can compare models; expressions
// also record quadratic (variable, variable, coefficient) terms, as IloExpr holds them.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using IloNum                      = double;
using IloInt                      = long;
using IloBool                     = IloInt;
inline constexpr IloBool IloFalse = 0;  // a macro in real Concert; compared the same way

inline constexpr IloNum IloInfinity = 1e20;

// The objects behind the handles. Real Concert frees them all at env.end(); the stub keeps them
// in one arena that env.end() empties, and that frees what is left at exit - the operators
// below make their results with IloEnv{}, and most tests never end their env. Locked: real
// envs are independent, so models built on several threads, one env each, must work here too.
class IloStubArena {
 public:
  template <typename T>
  static T* own(T* object) {
    const std::scoped_lock lock{mutex()};
    objects().emplace_back(object);  // a shared_ptr<void> keeps T's deleter
    return object;
  }
  static void clear() {
    const std::scoped_lock lock{mutex()};
    objects().clear();
  }

 private:
  static std::vector<std::shared_ptr<void>>& objects() {
    static std::vector<std::shared_ptr<void>> owned;
    return owned;
  }
  static std::mutex& mutex() {
    static std::mutex m;
    return m;
  }
};

#include <iostream>
class IloEnv {
 public:
  void end() { IloStubArena::clear(); }
  std::ostream& getNullStream() const { return std::cout; }
};

struct IloNumVarI {
  double lb, ub;
  std::uint8_t type;  // IloNumVar::Type
  bool semicont;
  std::string name;
};
struct IloExprI {
  std::vector<std::pair<IloNumVarI*, double>> terms;
  std::vector<std::tuple<IloNumVarI*, IloNumVarI*, double>> quadratic;
  double constant = 0;
};

class IloNumExprArg {
 public:
  IloNumVarI* var_ = nullptr;
  IloExprI* expr_  = nullptr;
  void appendTo(IloExprI& out, double k) const {
    if (var_) { out.terms.push_back({var_, k}); }
    if (expr_) {
      for (auto [v, c] : expr_->terms) {
        out.terms.push_back({v, c * k});
      }
      for (auto [u, v, c] : expr_->quadratic) {
        out.quadratic.emplace_back(u, v, c * k);
      }
      out.constant += expr_->constant * k;
    }
  }
};

class IloNumVar : public IloNumExprArg {
 public:
  enum Type { Int, Float, Bool };
  IloNumVar() = default;
  IloNumVar(IloNumVarI* impl) { var_ = impl; }  // NOLINT: Concert converts implicitly too
  IloNumVar(IloEnv, IloNum lb, IloNum ub, Type type = Float, const char* name = nullptr) {
    var_ = IloStubArena::own(new IloNumVarI{lb, ub, static_cast<std::uint8_t>(type), false, name ? name : ""});
  }
  IloNumVarI* getImpl() const { return var_; }
  IloNum getLB() const { return var_->lb; }
  IloNum getUB() const { return var_->ub; }
  void setLB(IloNum v) const { var_->lb = v; }
  void setUB(IloNum v) const { var_->ub = v; }
  void setBounds(IloNum lb, IloNum ub) const {
    var_->lb = lb;
    var_->ub = ub;
  }
  const char* getName() const { return var_->name.c_str(); }
};
class IloSemiContVar : public IloNumVar {
 public:
  IloSemiContVar(IloEnv env, IloNum sclb, IloNum ub, Type type = Float, const char* name = nullptr)
      : IloNumVar(env, sclb, ub, type, name) {
    var_->semicont = true;
  }
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
    ++ended;
  }
  static inline std::size_t ended = 0;  // tests check temporaries are ended
 private:
  std::vector<IloNumVar>* v_;
};
class IloNumArray {
 public:
  explicit IloNumArray(IloEnv) : v_(new std::vector<IloNum>) {}
  void add(IloNum x) { v_->push_back(x); }
  IloInt getSize() const { return static_cast<IloInt>(v_->size()); }
  IloNum operator[](IloInt i) const { return (*v_)[static_cast<std::size_t>(i)]; }
  void resize(std::size_t n) { v_->resize(n); }
  IloNum& at(std::size_t i) { return (*v_)[i]; }
  void end() {
    delete v_;
    v_ = nullptr;
    ++ended;
  }
  static inline std::size_t ended = 0;

 private:
  std::vector<IloNum>* v_;
};

// Like Concert: no IloExpr(number). A constant expression needs its env -
// IloExpr(env, value) - and a comparison with a number takes the number directly
// (expr <= 1), so `IloExpr{1}` must not compile here either.
class IloExpr : public IloNumExprArg {
 public:
  // NOLINTNEXTLINE
  IloExpr(IloEnv, IloNum value = 0) {
    expr_           = IloStubArena::own(new IloExprI);
    expr_->constant = value;
  }
  IloExpr(const IloNumExprArg& a) {
    expr_ = IloStubArena::own(new IloExprI);
    a.appendTo(*expr_, 1.0);
  }
  IloExpr& operator+=(const IloNumExprArg& a) {
    a.appendTo(*expr_, 1.0);
    return *this;
  }
  void end() {}
};
inline IloExpr operator*(const IloNumExprArg& a, double k) {
  IloExpr r(IloEnv{});
  a.appendTo(*r.expr_, k);
  return r;
}
inline IloExpr operator*(double k, const IloNumExprArg& a) {
  return a * k;
}
// Concert's IloNumExprArg * IloNumExprArg: (a + c)(b + d) = ab + ad + cb + cd, for linear a and b.
// Past degree 2 real Concert refuses the model when it is extracted; the stub only records
// what a product of two linear expressions makes.
inline IloExpr operator*(const IloNumExprArg& a, const IloNumExprArg& b) {
  IloExprI left;
  IloExprI right;
  a.appendTo(left, 1.0);
  b.appendTo(right, 1.0);
  IloExpr r(IloEnv{});
  for (auto [u, cu] : left.terms) {
    for (auto [v, cv] : right.terms) {
      r.expr_->quadratic.emplace_back(u, v, cu * cv);
    }
    r.expr_->terms.push_back({u, cu * right.constant});
  }
  for (auto [v, cv] : right.terms) {
    r.expr_->terms.push_back({v, cv * left.constant});
  }
  r.expr_->constant = left.constant * right.constant;
  return r;
}
inline IloExpr operator+(const IloNumExprArg& a, const IloNumExprArg& b) {
  IloExpr r(IloEnv{});
  a.appendTo(*r.expr_, 1);
  b.appendTo(*r.expr_, 1);
  return r;
}
inline IloExpr operator-(const IloNumExprArg& a, const IloNumExprArg& b) {
  IloExpr r(IloEnv{});
  a.appendTo(*r.expr_, 1);
  b.appendTo(*r.expr_, -1);
  return r;
}
inline IloExpr operator-(const IloNumExprArg& a, IloNum b) {
  IloExpr r(IloEnv{});
  a.appendTo(*r.expr_, 1);
  r.expr_->constant -= b;
  return r;
}
inline IloExpr IloSum(const IloNumVarArray& a) {
  IloExpr e(IloEnv{});
  for (IloInt i = 0; i < a.getSize(); ++i) {
    e += a[i];
  }
  return e;
}

struct IloConstraintI {
  std::vector<std::pair<IloNumVarI*, double>> terms;
  double rhs;
  std::int8_t sense;  // -1 <=, 0 ==, 1 >=
  std::string name;
};
class IloConstraint {
 public:
  IloConstraintI* impl_;
  // The name lives on the constraint, not the handle: setName through any copy names it.
  void setName(const char* name) const { impl_->name = name != nullptr ? name : ""; }
  const char* getName() const { return impl_->name.c_str(); }
};
using IloRange = IloConstraint;  // Concert's comparison operators give an IloRange
inline IloConstraint ilostub_make(const IloNumExprArg& a, const IloNumExprArg& b, std::int8_t sense) {
  IloExprI lhs;
  a.appendTo(lhs, 1.0);
  b.appendTo(lhs, -1.0);
  return IloConstraint{IloStubArena::own(new IloConstraintI{std::move(lhs.terms), -lhs.constant, sense, {}})};
}
inline IloConstraint operator<=(const IloNumExprArg& a, const IloNumExprArg& b) {
  return ilostub_make(a, b, -1);
}
inline IloConstraint operator>=(const IloNumExprArg& a, const IloNumExprArg& b) {
  return ilostub_make(a, b, 1);
}
inline IloConstraint operator==(const IloNumExprArg& a, const IloNumExprArg& b) {
  return ilostub_make(a, b, 0);
}
inline IloConstraint operator<=(const IloNumExprArg& a, IloNum b) {
  return ilostub_make(a, IloExpr(IloEnv{}, b), -1);
}
inline IloConstraint operator>=(const IloNumExprArg& a, IloNum b) {
  return ilostub_make(a, IloExpr(IloEnv{}, b), 1);
}
inline IloConstraint operator==(const IloNumExprArg& a, IloNum b) {
  return ilostub_make(a, IloExpr(IloEnv{}, b), 0);
}

// IloModel is not here: as in real Concert, it lives in <ilconcert/ilomodel.h>, which
// <ilcplex/ilocplex.h> includes. Code that builds a model includes one of those.
