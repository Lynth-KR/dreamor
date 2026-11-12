// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Ref<T>, a reference to an entity of a frozen EntityVector.
#pragma once

#include <bit>
#include <compare>
#include <cstdint>
#include <functional>
#include <type_traits>

#include "dreamor/internal/base/primary_key.hpp"
#include "dreamor/logger.hpp"

namespace dreamor {
template <typename T>
class Ref;
}  // namespace dreamor

namespace dreamor::internal::inline index_set {

// The library's access to the private parts of a Ref: make() creates a Ref from the address of an entity of a frozen
// EntityVector, and address() reads that address back.
struct RefAccess {
  template <typename T>
  [[nodiscard]] static Ref<T> make(const T* entity) noexcept {
    return Ref<T>{entity};
  }

  template <typename T>
  [[nodiscard]] static const T* address(const Ref<T>& ref) noexcept {
    return ref.entity_;
  }
};

template <typename T>
struct IsRef : std::false_type {};

template <typename T>
struct IsRef<Ref<T>> : std::true_type {};

template <typename T>
concept ref = IsRef<std::remove_cvref_t<T>>::value;

}  // namespace dreamor::internal::inline index_set

namespace dreamor {

/// @section Entities

/// A reference to one entity of a frozen `EntityVector<T>`, or to no entity.
///
/// An entity column holds a `Ref`. A member of an entity that refers to another entity holds a `Ref` too.
///
/// A `Ref` does not own its entity, and it gives only `const` access to the entity. A default-constructed `Ref` is
/// empty: it refers to no entity. `find` also returns an empty `Ref` if no entity has the key. Only a frozen
/// `EntityVector` creates a `Ref` that refers to an entity. `at` and `find` return such a `Ref`, and a set built from
/// the vector holds such `Ref`s in its entity columns.
///
/// Two `Ref`s are equal if they refer to the same entity, or if both are empty. `std::hash` hashes a `Ref` by the
/// address of its entity. `<=>` orders `Ref`s by the primary keys of their entities, not by address, so a set sorted
/// by an entity column has the same order in every run. In a name and in `std::format`, a `Ref` is written as its
/// entity's primary key.
///
/// Takes: `T` must not be `const`, `volatile`, a pointer or a reference.
template <typename T>
class Ref {
  static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>,
                "Write Ref<Machine>, not Ref<const Machine>: a Ref always reads its entity and never changes it.");
  static_assert(!std::is_reference_v<T> && !std::is_pointer_v<T>,
                "A Ref refers to an entity: Ref<Machine>, not Ref<Machine*> or Ref<Machine&>.");

 public:
  using element_type = T;

  Ref() = default;

  /// Checks whether the `Ref` refers to an entity.
  ///
  /// Returns: True if the `Ref` refers to an entity; false otherwise.
  [[nodiscard]] explicit operator bool() const noexcept { return entity_ != nullptr; }

  /// Returns a pointer to the entity that the `Ref` refers to.
  ///
  /// Contract: ref_not_empty
  [[nodiscard]] const T* operator->() const noexcept {
    checkRefers();
    return entity_;
  }

  /// Returns the entity that the `Ref` refers to.
  ///
  /// Contract: ref_not_empty
  [[nodiscard]] const T& operator*() const noexcept {
    checkRefers();
    return *entity_;
  }

  [[nodiscard]] friend bool operator==(const Ref&, const Ref&) = default;

  /// Returns the order of two `Ref`s by the primary keys of their entities.
  ///
  /// An empty `Ref` comes before every `Ref` that refers to an entity. Two empty `Ref`s are equivalent. In one
  /// `EntityVector`, no two entities have the same primary key, so `<=>` agrees with `==`. Two `Ref`s to entities of
  /// different vectors that have the same primary key are equivalent but not equal.
  ///
  /// Takes: The primary key of `T` must support `<=>`. Otherwise a comparison of two `Ref`s with `<=>` does not
  /// compile.
  [[nodiscard]] friend std::weak_ordering operator<=>(const Ref& a, const Ref& b)
    requires requires(const T& entity) {
      std::compare_three_way{}(internal::primaryKeyOf(entity), internal::primaryKeyOf(entity));
    }
  {
    if (a.entity_ == b.entity_) { return std::weak_ordering::equivalent; }
    if (a.entity_ == nullptr) { return std::weak_ordering::less; }
    if (b.entity_ == nullptr) { return std::weak_ordering::greater; }
    const auto order = std::compare_three_way{}(internal::primaryKeyOf(*a.entity_), internal::primaryKeyOf(*b.entity_));
    if (order < 0) { return std::weak_ordering::less; }
    if (order > 0) { return std::weak_ordering::greater; }
    return std::weak_ordering::equivalent;
  }

 private:
  friend struct internal::RefAccess;

  explicit Ref(const T* entity) noexcept : entity_{entity} {}

  /// @contract ref_not_empty: A `Ref` that is read through `->` or `*` refers to an entity.

  void checkRefers() const noexcept {
#ifndef NDEBUG
    if (entity_ == nullptr) {
      DREAMOR_DEBUG_FATAL("Ref: an empty Ref read - it refers to no entity. Check it first: if (ref) ...");
    }
#endif
  }

  const T* entity_ = nullptr;
};

/// @hidden - dreamor's own indexes find it by argument-dependent lookup
template <typename T>
[[nodiscard]] std::uint64_t hashElement(const Ref<T>& ref) noexcept {
  // TupleHash hashes a Ref by the address that it holds, widened to 64 bits. Col has an overload of the same kind for a
  // column.
  return static_cast<std::uint64_t>(std::bit_cast<std::uintptr_t>(internal::RefAccess::address(ref)));
}

}  // namespace dreamor

template <typename T>
struct std::hash<dreamor::Ref<T>> {
  [[nodiscard]] std::size_t operator()(const dreamor::Ref<T>& ref) const noexcept {
    return static_cast<std::size_t>(dreamor::hashElement(ref));
  }
};
