// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// HighsEnv: the model that the HiGHS backend writes, and the Highs object that solves the model.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/backend/var_table_core.hpp"
#include "dreamor/internal/highs/error.hpp"
#include "dreamor/internal/highs/expression.hpp"
#include "dreamor/logger.hpp"

#include "dreamor/internal/base/third_party_headers.hpp"
DREAMOR_BEGIN_THIRD_PARTY_HEADERS
#include "Highs.h"
DREAMOR_END_THIRD_PARTY_HEADERS

namespace dreamor::highs::detail {
struct HighsEnvAccess;
}  // namespace dreamor::highs::detail

namespace dreamor::highs::detail {

// Throws HighsCallError if a HiGHS call returned an error. A warning does not throw, because HiGHS logs it.
inline void highsCall(const char* call, HighsStatus status) {
  if (status == HighsStatus::kError) { throw dreamor::highs::HighsCallError{call, status}; }
}

// Converts a count to HighsInt, the type in which HiGHS counts, and throws if the count does not fit.
[[nodiscard]] inline HighsInt highsCount(std::size_t count) {
  if (count > static_cast<std::size_t>(std::numeric_limits<HighsInt>::max())) {
    throw std::length_error("dreamor::highs: more entries than a HighsInt counts.");
  }
  return static_cast<HighsInt>(count);
}

// A workspace for each thread. It adds up the terms of an expression over the same column before HiGHS sees them,
// in time linear in the number of terms. Each column has a mark that holds the position of its merged entry, and a
// mark is valid only while its pass is the current pass. The arrays grow to the largest model that the thread has
// handed over.
class HighsWorkspace {
 public:
  static HighsWorkspace& local() {
    thread_local HighsWorkspace workspace;
    return workspace;
  }

  // Appends (indices, coefficients), over a model of `columns` columns, to (to_columns, to_values). The terms over
  // one column are added together, in the order in which each column first appears, and an entry whose sum is zero
  // is dropped. A short sum, as most rows are, is merged in place: each term is compared with the entries that this
  // call has already appended. A longer sum is merged in the workspace through the marks, and then copied over with
  // one insert.
  void appendMerged(std::span<const HighsInt> indices,
                    std::span<const double> coefficients,
                    std::size_t columns,
                    std::vector<HighsInt>& to_columns,
                    std::vector<double>& to_values) {
    if (indices.size() <= kCompared) {
      const std::size_t first = to_columns.size();
      appendCompared(indices, coefficients, to_columns, to_values);
      dropZeros(to_columns, to_values, first);
      return;
    }
    merge(indices, coefficients, columns);
    to_columns.insert(to_columns.end(), merged_columns_.begin(), merged_columns_.end());
    to_values.insert(to_values.end(), merged_values_.begin(), merged_values_.end());
  }

  // Merges (indices, coefficients) into columns() and values(), as appendMerged does.
  void merge(std::span<const HighsInt> indices, std::span<const double> coefficients, std::size_t columns) {
    merged_columns_.clear();
    merged_values_.clear();
    if (indices.size() <= kCompared) {
      appendCompared(indices, coefficients, merged_columns_, merged_values_);
    } else {
      appendStamped(indices, coefficients, columns);
    }
    dropZeros(merged_columns_, merged_values_, 0);
  }

  [[nodiscard]] std::span<const HighsInt> columns() const noexcept { return merged_columns_; }

  [[nodiscard]] std::span<const double> values() const noexcept { return merged_values_; }

  // Returns the cost of every column, which is zero for a column that the objective does not contain. A cost array
  // has a slot for every column, so the terms over one column add up in its slot, and terms that cancel leave a cost
  // of zero. No merge is needed.
  [[nodiscard]] std::span<const double> costs(const dreamor::highs::HighsLinearExpression& objective,
                                              std::size_t columns) {
    fillCosts(objective, columns, costs_);
    return costs_;
  }

  static void fillCosts(const dreamor::highs::HighsLinearExpression& objective,
                        std::size_t columns,
                        std::vector<double>& costs) {
    costs.assign(columns, 0.0);
    const std::span<const HighsInt> indices    = objective.indices();
    const std::span<const double> coefficients = objective.coefficients();
    for (std::size_t k = 0; k < indices.size(); ++k) {
      costs[static_cast<std::size_t>(indices[k])] += coefficients[k];
    }
  }

  // Builds the Hessian of the quadratic terms in the form that passHessian takes: the lower triangle, column by
  // column. HiGHS minimizes c'x + 1/2 x'Qx, so c x_i^2 becomes Q_ii = 2c, and c x_i x_j with i > j becomes
  // Q_ij = c. Entries at the same position are added together, and zeros are dropped. The rows of each column are in
  // ascending order, so the diagonal entry comes first.
  void hessian(const dreamor::highs::HighsQuadraticExpression& objective, std::size_t columns) {
    const std::span<const HighsInt> rows    = objective.first();
    const std::span<const HighsInt> cols    = objective.second();
    const std::span<const double> quadratic = objective.quadraticCoefficients();
    // Sorts the terms by column with a counting sort.
    bucket_start_.assign(columns + 1, 0);
    for (const HighsInt column : cols) {
      ++bucket_start_[static_cast<std::size_t>(column) + 1];
    }
    for (std::size_t j = 0; j < columns; ++j) {
      bucket_start_[j + 1] += bucket_start_[j];
    }
    bucket_rows_.resize(rows.size());
    bucket_values_.resize(rows.size());
    fill_.assign(bucket_start_.begin(), bucket_start_.end() - 1);
    for (std::size_t k = 0; k < rows.size(); ++k) {
      const std::size_t at = fill_[static_cast<std::size_t>(cols[k])]++;
      bucket_rows_[at]     = rows[k];
      bucket_values_[at]   = quadratic[k];
    }
    // Merges the entries of each column by row, sorts them by row, and doubles the diagonal entry.
    hessian_start_.assign(1, 0);
    hessian_index_.clear();
    hessian_value_.clear();
    for (std::size_t j = 0; j < columns; ++j) {
      const std::size_t from = bucket_start_[j];
      const std::size_t to   = bucket_start_[j + 1];
      merge(std::span<const HighsInt>{bucket_rows_}.subspan(from, to - from),
            std::span<const double>{bucket_values_}.subspan(from, to - from),
            columns);
      sortMergedByIndex();
      for (std::size_t k = 0; k < merged_columns_.size(); ++k) {
        const bool diagonal = std::cmp_equal(merged_columns_[k], j);
        hessian_index_.push_back(merged_columns_[k]);
        hessian_value_.push_back(diagonal ? kDiagonalScale * merged_values_[k] : merged_values_[k]);
      }
      hessian_start_.push_back(highsCount(hessian_index_.size()));
    }
  }

  [[nodiscard]] std::span<const HighsInt> hessianStart() const noexcept { return hessian_start_; }

  [[nodiscard]] std::span<const HighsInt> hessianIndex() const noexcept { return hessian_index_; }

  [[nodiscard]] std::span<const double> hessianValue() const noexcept { return hessian_value_; }

  // Returns the start array of an empty Hessian over `columns` columns. Passing it removes a quadratic objective.
  [[nodiscard]] std::span<const HighsInt> emptyHessianStart(std::size_t columns) {
    hessian_start_.assign(columns + 1, 0);
    return hessian_start_;
  }

 private:
  // Adds each term to the entry of its column if this call has already appended one, and appends a new entry
  // otherwise. Each term costs a few comparisons.
  static void appendCompared(std::span<const HighsInt> indices,
                             std::span<const double> coefficients,
                             std::vector<HighsInt>& to_columns,
                             std::vector<double>& to_values) {
    const std::size_t first = to_columns.size();
    for (std::size_t k = 0; k < indices.size(); ++k) {
      std::size_t at = first;
      while (at < to_columns.size() && to_columns[at] != indices[k]) {
        ++at;
      }
      if (at == to_columns.size()) {
        to_columns.push_back(indices[k]);
        to_values.push_back(coefficients[k]);
      } else {
        to_values[at] += coefficients[k];
      }
    }
  }

  // Adds each term to the entry of its column in merged_columns_ and merged_values_. If the column's mark belongs to
  // the current pass, the mark gives the entry's slot, so each term costs one read, however long the sum is. The
  // high half of a mark holds its pass, and the low half holds the slot, so each column costs 8 bytes of workspace.
  void appendStamped(std::span<const HighsInt> indices, std::span<const double> coefficients, std::size_t columns) {
    if (indices.size() > std::numeric_limits<std::uint32_t>::max()) {
      throw std::length_error("dreamor::highs: more terms in one sum than a mark's slot counts.");
    }
    beginPass(columns);
    const std::uint64_t current = std::uint64_t{pass_} << kSlotBits;
    for (std::size_t k = 0; k < indices.size(); ++k) {
      std::uint64_t& mark = marks_[static_cast<std::size_t>(indices[k])];
      if ((mark >> kSlotBits) != pass_) {
        mark = current | merged_columns_.size();
        merged_columns_.push_back(indices[k]);
        merged_values_.push_back(coefficients[k]);
      } else {
        merged_values_[mark & kSlotMask] += coefficients[k];
      }
    }
  }

  void beginPass(std::size_t columns) {
    if (marks_.size() < columns) { marks_.resize(columns, 0); }
    if (++pass_ == 0) {  // The pass number wrapped around: clear the marks, so that no old mark looks current.
      std::ranges::fill(marks_, 0U);
      pass_ = 1;
    }
  }

  // Removes the entries whose value is zero, from position `first` on.
  static void dropZeros(std::vector<HighsInt>& columns, std::vector<double>& values, std::size_t first) {
    std::size_t kept = first;
    for (std::size_t k = first; k < columns.size(); ++k) {
      if (std::fpclassify(values[k]) != FP_ZERO) {
        columns[kept] = columns[k];
        values[kept]  = values[k];
        ++kept;
      }
    }
    columns.resize(kept);
    values.resize(kept);
  }

  // Sorts the merged entries by ascending index. They are the rows of one Hessian column, which are few, so an
  // insertion sort is enough.
  void sortMergedByIndex() {
    for (std::size_t k = 1; k < merged_columns_.size(); ++k) {
      const HighsInt column = merged_columns_[k];
      const double value    = merged_values_[k];
      std::size_t at        = k;
      while (at > 0 && merged_columns_[at - 1] > column) {
        merged_columns_[at] = merged_columns_[at - 1];
        merged_values_[at]  = merged_values_[at - 1];
        --at;
      }
      merged_columns_[at] = column;
      merged_values_[at]  = value;
    }
  }

  // HiGHS's objective has the factor 1/2 in 1/2 x'Qx, so the term c x_i^2 becomes Q_ii = 2c.
  static constexpr double kDiagonalScale = 2.0;
  // The longest sum that is merged by comparing its terms. Up to this length, the comparisons cost less than reading
  // the marks, which lie far apart in the memory of a large model.
  static constexpr std::size_t kCompared = 8;

  static constexpr unsigned kSlotBits      = 32;
  static constexpr std::uint64_t kSlotMask = (std::uint64_t{1} << kSlotBits) - 1;

  std::vector<std::uint64_t> marks_;  // For each column: (pass << kSlotBits) | slot.
  std::uint32_t pass_{0};
  std::vector<HighsInt> merged_columns_;
  std::vector<double> merged_values_;
  std::vector<double> costs_;
  std::vector<std::size_t> bucket_start_;
  std::vector<std::size_t> fill_;
  std::vector<HighsInt> bucket_rows_;
  std::vector<double> bucket_values_;
  std::vector<HighsInt> hessian_start_;
  std::vector<HighsInt> hessian_index_;
  std::vector<double> hessian_value_;
};

/// @contract highs_expression_of_its_environment: Every variable of an expression or a `VarTable` that a function
/// receives together with a `HighsEnv` belongs to that `HighsEnv`.
///
/// `addConstr`, `setObjective`, `minimize`, `maximize`, `val` and `VarTable::solution` check this contract. The
/// check compares the `HighsEnv` that the expression or the table records with the `HighsEnv` that the function
/// receives. An expression or a table without variables records no `HighsEnv`, and the check skips it.

inline void checkHighsModel([[maybe_unused]] const dreamor::highs::HighsEnv* expression_env,
                            [[maybe_unused]] const dreamor::highs::HighsEnv& env,
                            [[maybe_unused]] const char* call) {
#ifndef NDEBUG
  if (expression_env != nullptr && expression_env != &env) {
    DREAMOR_DEBUG_FATAL(
        "dreamor::highs::{}: the expression's variables belong to another HighsEnv - a broken contract "
        "(dreamor/highs.hpp).",
        call);
  }
#endif
}

}  // namespace dreamor::highs::detail

namespace dreamor::highs {

/// @section dreamor::highs
///
/// The parts of this section write a model for HiGHS and solve it through HiGHS's own C++ API. That API is a matrix
/// API: a column is an index, and a row is its bounds with arrays of column indices and coefficients. The API has no
/// expressions in which to write a model. This backend adds the declarative interface of highspy, the Python
/// interface of HiGHS, with highspy's names written in dreamor's case:
///
/// - highspy's `Highs` is `HighsEnv`, which holds the model as written and the `Highs` object that solves it.
/// - `highs_var` is `HighsVar`, `highs_linear_expression` is `HighsLinearExpression`, and `highs_cons` is
///   `HighsCons`.
/// - `Highs.addVariable` is `HighsEnv::addVariable`.
/// - `addConstr`, `setObjective`, `minimize`, `maximize` and `val` keep their names, as functions that take the
///   `HighsEnv` as their first argument.
/// - `qsum` is `sumExpr`.
///
/// In addition, a `VarTable` adds a family of variables over a set in one step.
///
/// In this section, a number is a value of an arithmetic type other than `bool`, or a column that holds such a value.
/// A linear operand is a number, a `HighsVar`, a `HighsLinearTerm` or a `HighsLinearExpression`. A row is also a
/// constraint of the model, as in HiGHS's API: a linear expression between a lower and an upper bound. In `VarTable`
/// and `sumExpr`, a row is still a row of a set.
///
/// This backend differs from highspy in these ways:
///
/// - A row has types of its own. A call that passes a row to `setObjective` does not compile.
/// - A comparison that has a quadratic operand does not compile, because HiGHS accepts quadratic terms only in the
///   objective.
/// - The `HighsEnv` collects the model and hands it to the `Highs` in one `passModel` call.
/// - `setObjective` also replaces the quadratic part of the objective, the Hessian.
/// - An operator whose left operand is an rvalue expression appends to that expression and does not copy it.
///
/// HiGHS rejects a row or a Hessian that names a column twice. `addConstr` and `setObjective` therefore add up the
/// terms over the same column before they pass an expression to HiGHS.
///
/// To build a program on this backend, add the `include/highs` directory of HiGHS to the include path and link
/// `libhighs`. The `libhighs` that the OR-Tools prebuilt ships also works.
///
/// On WebAssembly with Emscripten, a program that uses HiGHS must be linked with a stack of 8 MB, which the option
/// `-sSTACK_SIZE=8MB` sets. The simplex and MIP solvers of HiGHS overflow Emscripten's default stack of 64 KB, and
/// nothing detects the overflow.
///
/// Guide: guide/backends.md

/// A HiGHS model as written, together with the `Highs` object that solves it.
///
/// `HighsEnv::addVariable`, the constructor of a `VarTable`, `addConstr` and `setObjective` write the model. Until
/// the first call to `highs()`, the `HighsEnv` collects what they write. The first call to `highs()` hands the
/// collected model to the `Highs` in one `passModel` call. After that call, each write goes to the `Highs` at once.
/// `VarTable::solution`, `val`, `minimize` and `maximize` call `highs()` too.
///
/// The `Highs` that `highs()` returns therefore holds everything written so far. HiGHS's own API on that `Highs`,
/// such as its options, `run`, `getLp`, `writeModel` and `changeColBounds`, can be used directly.
///
/// A `HighsEnv` cannot be copied or moved, because each of its variables refers to it. A `HighsEnv` corresponds to
/// highspy's `Highs`, which likewise holds both the model and the solver.
///
/// Throws: `HighsCallError` if HiGHS rejects the collected model at the first call to `highs()`.
/// Cost: A model that is written completely before the first call to `highs()` is built fastest. After that call,
/// each row that `addConstr` adds is a separate call to HiGHS, which costs more than the row itself.
class HighsEnv {
 public:
  /// @anchor highsenv-constructor
  /// Constructs an empty model and a `Highs` that has HiGHS's default options.
  HighsEnv() {
    model_.lp_.a_matrix_.format_ = MatrixFormat::kRowwise;
    model_.lp_.a_matrix_.start_.assign(1, 0);
  }

  /// @anchor highsenv-constructor-options
  /// Constructs an empty model and a `Highs` that has the options `options`.
  ///
  /// The options are passed to the `Highs` before the model is handed over. An option such as a silent log therefore
  /// applies to the `passModel` call too.
  ///
  /// Throws: `HighsCallError` if HiGHS rejects `options`.
  explicit HighsEnv(const HighsOptions& options) : HighsEnv{} {
    detail::highsCall("passOptions", highs_.passOptions(options));
  }

  // Each variable points to its environment, so an environment must not move.
  HighsEnv(const HighsEnv&)            = delete;
  HighsEnv& operator=(const HighsEnv&) = delete;
  HighsEnv(HighsEnv&&)                 = delete;
  HighsEnv& operator=(HighsEnv&&)      = delete;
  ~HighsEnv()                          = default;

  /// Returns the `Highs`, which holds everything written to the model so far.
  ///
  /// If the model has a variable or a row, the first call hands the collected model to the `Highs` in one
  /// `passModel` call. Later calls return the `Highs` as it is. If handing the model over fails, the model cannot be
  /// handed over again, and every later call throws the same exception.
  ///
  /// Throws: `HighsCallError` if HiGHS rejects the collected model; `std::length_error` if the collected model has
  /// more columns or rows than `HighsInt` can count.
  [[nodiscard]] Highs& highs() {
    pass();
    return highs_;
  }

  /// @overload
  [[nodiscard]] const Highs& highs() const {
    pass();
    return highs_;
  }

  /// Adds one variable to the model.
  ///
  /// For a family of variables over a set, use a `VarTable`.
  ///
  /// Takes: `lb` and `ub` must be numbers: values of an arithmetic type other than `bool`, or columns that hold such
  /// a value. If `lb` is left out, it is 0. If `ub` is left out, it is `kHighsInf`. `type` is HiGHS's variable type;
  /// if it is left out, it is `HighsVarType::kContinuous`. If `name` is left out or empty, the variable has no name.
  /// Returns: The new variable.
  /// Throws: `HighsCallError` if HiGHS rejects the variable or its name after the first call to `highs()`.
  template <typename Lower = double, typename Upper = double>
    requires(is_highs_scalar<Lower> && is_highs_scalar<Upper>)
  [[nodiscard]] HighsVar addVariable(Lower lb              = 0,
                                     Upper ub              = kHighsInf,
                                     HighsVarType type     = HighsVarType::kContinuous,
                                     std::string_view name = {}) {
    return addOneVariable(static_cast<double>(lb), static_cast<double>(ub), type, name);
  }

  /// @hidden - exists only to reject `nullptr` as a name with a message
  template <typename Lower, typename Upper>
  HighsVar addVariable(Lower lb, Upper ub, HighsVarType type, std::nullptr_t /*name*/) {
    static_assert(internal::always_false<Lower>, DREAMOR_INTERNAL_NULL_NAME_MESSAGE);
    return addVariable(lb, ub, type);
  }

 private:
  friend struct detail::HighsEnvAccess;

  HighsVar addOneVariable(double lb, double ub, HighsVarType type, std::string_view name);

  // Hands the collected model to the Highs once, and leaves the collected model empty. pass() is const because the
  // Highs of a const environment can be read too, so the members that pass() changes are mutable. passed_ is set only
  // once the model is handed over, because the writes of model.hpp route by it. If handing over fails, the model
  // cannot be handed over again, as passModel has taken it, so the exception is kept and thrown again by every later
  // call, as a std::shared_future's get() rethrows its exception.
  void pass() const {
    if (passed_) { return; }
    if (failure_) { std::rethrow_exception(failure_); }
    try {
      handOver();
    } catch (...) {
      failure_ = std::current_exception();
      throw;
    }
    passed_ = true;
  }

  void handOver() const {
    HighsLp& lp = model_.lp_;
    if (lp.col_cost_.empty() && lp.row_lower_.empty()) {
      if (lp.sense_ != ObjSense::kMinimize) {
        detail::highsCall("changeObjectiveSense", highs_.changeObjectiveSense(lp.sense_));
      }
      if (std::fpclassify(lp.offset_) != FP_ZERO) {
        detail::highsCall("changeObjectiveOffset", highs_.changeObjectiveOffset(lp.offset_));
      }
      return;
    }
    lp.num_col_           = detail::highsCount(lp.col_cost_.size());
    lp.num_row_           = detail::highsCount(lp.row_lower_.size());
    lp.a_matrix_.num_col_ = lp.num_col_;
    lp.a_matrix_.num_row_ = lp.num_row_;
    if (named_) {  // HiGHS takes a name for every column and row, so an unnamed one gets an empty name.
      lp.col_names_.resize(lp.col_cost_.size());
      lp.row_names_.resize(lp.row_lower_.size());
    }
    // Columns added after a quadratic objective have no quadratic terms, so the Hessian gets empty columns for them.
    HighsHessian& hessian = model_.hessian_;
    if (hessian.dim_ > 0 && hessian.dim_ < lp.num_col_) {
      hessian.start_.resize(static_cast<std::size_t>(lp.num_col_) + 1, hessian.start_.back());
      hessian.dim_ = lp.num_col_;
    }
    HighsModel model = std::move(model_);
    model_           = HighsModel{};
    detail::highsCall("passModel", highs_.passModel(std::move(model)));
  }

  // The model as written until it is handed over, with its matrix stored row by row. The name arrays have no entry
  // until a column or a row is named: in a model that names nothing, an empty std::string of 32 bytes for each row
  // would be most of the memory that the row takes.
  mutable HighsModel model_;
  mutable Highs highs_;
  mutable bool passed_{false};
  mutable std::exception_ptr failure_;  // What handing the model over threw, if it failed.
  bool named_{false};                   // True if a column or a row of model_ has a name.
};

}  // namespace dreamor::highs
