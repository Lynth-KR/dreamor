// SPDX-License-Identifier: Apache-2.0
// File: internal/inline_vector.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// A vector that keeps its first N elements inside the object and moves to the heap past them, as
// FormatBuffer keeps a name: what holds a few values - an expression of a few terms, as most rows of a
// model are - without an allocation, and anything longer as a std::vector would. Each one owns its
// storage, so any number live at once: an expression is a value that is kept, copied and returned. The
// elements are trivially copyable (indices, coefficients), so growing and moving copy their bytes.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

namespace dreamor::internal {

template <typename T, std::size_t N>
  requires(std::is_trivially_copyable_v<T> && N > 0)
class InlineVector {
 public:
  using value_type = T;

  // Kept true throughout: on the heap, heap_ holds exactly the size_ elements.
  InlineVector() noexcept           = default;
  InlineVector(const InlineVector&) = default;  // heap_ copies with no storage when other's is inline
  // A vector moved from is left empty and inline: a std::vector moved from by construction is empty.
  InlineVector(InlineVector&& other) noexcept
      : inline_{other.inline_}, heap_{std::move(other.heap_)}, size_{std::exchange(other.size_, 0)} {}
  InlineVector& operator=(const InlineVector& other) {
    if (this == &other) { return *this; }
    if (onHeap() || other.size_ > N) {
      heap_.assign(other.begin(), other.end());  // a heap stays one: its storage is kept
    } else {
      std::copy_n(other.inline_.begin(), other.size_, inline_.begin());
    }
    size_ = other.size_;
    return *this;
  }
  InlineVector& operator=(InlineVector&& other) noexcept {
    if (this == &other) { return *this; }
    inline_ = other.inline_;
    heap_   = std::move(other.heap_);  // frees this one's heap, if any
    other.heap_.clear();               // what a move assignment leaves is only valid: make it empty
    size_ = std::exchange(other.size_, 0);
    return *this;
  }
  ~InlineVector() = default;

  [[nodiscard]] T* data() noexcept { return onHeap() ? heap_.data() : inline_.data(); }
  [[nodiscard]] const T* data() const noexcept { return onHeap() ? heap_.data() : inline_.data(); }
  [[nodiscard]] T* begin() noexcept { return data(); }
  [[nodiscard]] T* end() noexcept { return data() + size_; }
  [[nodiscard]] const T* begin() const noexcept { return data(); }
  [[nodiscard]] const T* end() const noexcept { return data() + size_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] T& operator[](std::size_t position) noexcept { return data()[position]; }
  [[nodiscard]] const T& operator[](std::size_t position) const noexcept { return data()[position]; }

  void reserve(std::size_t capacity) {
    if (onHeap()) {
      heap_.reserve(capacity);
    } else if (capacity > N) {
      moveToHeap(capacity);
    }
  }
  void pushBack(T value) {
    if (onHeap()) {
      heap_.push_back(value);
    } else if (size_ < N) {
      inline_[size_] = value;
    } else {
      moveToHeap(2 * N);
      heap_.push_back(value);
    }
    ++size_;
  }

 private:
  // Once on the heap, a vector stays there: its storage is kept for what comes next.
  [[nodiscard]] bool onHeap() const noexcept { return heap_.capacity() != 0; }

  void moveToHeap(std::size_t capacity) {
    heap_.reserve(capacity);
    heap_.assign(inline_.begin(), inline_.begin() + static_cast<std::ptrdiff_t>(size_));
  }

  std::array<T, N> inline_{};
  std::vector<T> heap_;  // no storage while the elements fit inline_
  std::size_t size_{0};
};

}  // namespace dreamor::internal
