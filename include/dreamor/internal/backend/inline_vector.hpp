// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// InlineVector, a vector that keeps up to N elements inside the object and moves them to the heap when there are more,
// as a short std::string keeps its characters. Most rows of a model are expressions of a few terms, and such an
// expression needs no allocation; a longer one grows as a std::vector does. Each vector owns its storage, so any number
// of vectors can exist at once, as an expression is a value that is kept, copied and returned. The elements are
// trivially copyable, such as indices and coefficients, so growing and moving copy their bytes.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

namespace dreamor::internal::inline backend {

template <typename T, std::size_t N>
  requires(std::is_trivially_copyable_v<T> && N > 0)
class InlineVector {
 public:
  using value_type = T;

  // Invariant: if the vector is on the heap, heap_ holds exactly the size_ elements.
  InlineVector() noexcept           = default;
  InlineVector(const InlineVector&) = default;  // If other is inline, the copy of heap_ has no storage.

  // A vector that is moved from is left empty and inline, as a std::vector that is moved from by construction is left
  // empty.
  InlineVector(InlineVector&& other) noexcept
      : inline_{other.inline_}, heap_{std::move(other.heap_)}, size_{std::exchange(other.size_, 0)} {}

  InlineVector& operator=(const InlineVector& other) {
    if (this == &other) { return *this; }
    if (onHeap() || other.size_ > N) {
      heap_.assign(other.begin(), other.end());  // A vector on the heap stays there, and keeps its storage.
    } else {
      std::copy_n(other.begin(), other.size_, inline_.begin());  // other may be on the heap with N or fewer elements.
    }
    size_ = other.size_;
    return *this;
  }

  InlineVector& operator=(InlineVector&& other) noexcept {
    if (this == &other) { return *this; }
    inline_ = other.inline_;
    heap_   = std::move(other.heap_);  // Frees the storage that this vector had on the heap, if any.
    other.heap_.clear();               // A std::vector after move assignment is only valid, so make it empty.
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
  // Once a vector is on the heap, it stays there, and its storage is kept for the elements that come next.
  [[nodiscard]] bool onHeap() const noexcept { return heap_.capacity() != 0; }

  void moveToHeap(std::size_t capacity) {
    heap_.reserve(capacity);
    heap_.assign(inline_.begin(), inline_.begin() + static_cast<std::ptrdiff_t>(size_));
  }

  std::array<T, N> inline_{};
  std::vector<T> heap_;  // Has no storage while the elements fit in inline_.
  std::size_t size_{0};
};

}  // namespace dreamor::internal::inline backend
