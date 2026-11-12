// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// EntityVector, the model's entities of one type, each held once by its primary key; the entity concept.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/internal/base/flat_index.hpp"
#include "dreamor/internal/base/primary_key.hpp"
#include "dreamor/internal/index_set/name_token.hpp"
#include "dreamor/internal/index_set/ref.hpp"
#include "dreamor/logger.hpp"

namespace dreamor::detail {

template <typename T>
struct IsStdTuple : std::false_type {};

template <typename... Ts>
struct IsStdTuple<std::tuple<Ts...>> : std::true_type {};

// The type that primaryKey() returns, which may be a reference or a value.
template <typename T>
using PrimaryKeyResult = decltype(internal::primaryKeyOf(std::declval<const T&>()));

// The argument types for which find, at and contains look up a std::string key without constructing a std::string:
// every type that converts to std::string_view, except std::string itself, which uses the key_type overload.
template <typename Key, typename Text>
concept text_key_probe = std::same_as<Key, std::string> && !std::same_as<std::remove_cvref_t<Text>, std::string> &&
                         std::is_convertible_v<const Text&, std::string_view>;

// Returns the key in the form that the index hashes, a std::tuple. A composite key is already a std::tuple; any other
// key is wrapped in a std::tuple of one element.
template <typename T>
[[nodiscard]] auto indexKeyOf(const T& entity) {
  using Result = PrimaryKeyResult<T>;
  if constexpr (IsStdTuple<std::remove_cvref_t<Result>>::value) {
    return internal::primaryKeyOf(entity);
  } else {
    return std::tuple<Result>{internal::primaryKeyOf(entity)};  // a reference stays a reference: no string is copied
  }
}

}  // namespace dreamor::detail

namespace dreamor {

/// @section Entities
///
/// The parts of this section hold a model's entities and refer to them. An entity is an object of the user's type,
/// such as a machine, an item or a route. An `EntityVector` holds the entities of one type, each one once, and
/// identifies each entity by its primary key. A `Ref` refers to one entity of a frozen `EntityVector`.
///
/// The entity type defines its primary key in one of two ways:
///
/// - With a `const` member function `primaryKey()`.
/// - With a free function `primaryKey(const T&)` in the namespace of the type, which argument-dependent lookup finds.
///   This form is for a type that cannot be changed.
///
/// If the type has both functions, the member function is used. The primary key is a value such as an id or a code.
/// A composite key is a `std::tuple` of such values, and may hold a `Ref` to an entity of another type, as a route
/// is keyed by its machine and its item.
///
/// A primary key does not lead back to its own type, either through a `Ref` in it or through the keys of the entities
/// that it refers to; such a type stops at compile time. An entity that belongs to another of its own type, such as an
/// item in a bill of materials, is keyed by a value of its own, and what it belongs to is a set of the two, such as
/// an `IndexSet<cParent, cChild>`.
///
/// In a name, the parts of a composite key are joined with `_`. To use another separator, the type defines
/// `primaryKeySeparator()`, which returns the separator as a value that converts to `std::string_view`. Like
/// `primaryKey()`, `primaryKeySeparator()` is a member function or a free function that argument-dependent lookup
/// finds.
///
/// Guide: guide/entities.md

/// A type that has a primary key that can be compared with `==`.
///
/// The type defines its primary key with a member function `primaryKey()` or with a free function
/// `primaryKey(const T&)`.
template <typename T>
concept entity = internal::primary_keyed<T> && std::equality_comparable<internal::PrimaryKeyOf<T>>;

/// A container of the model's entities of one type, each held once by its primary key.
///
/// An `EntityVector` is used in two phases, which `freeze()` separates:
///
/// - In the filling phase, before `freeze()`, the vector is filled with `reserve`, `insert`, `emplace` and `sort`.
///   The vector's entities are not read in this phase, because filling can still move them.
/// - In the reading phase, after `freeze()`, the vector is iterated, indexed by position, used as the source of a
///   set, and used for lookups with `find`, `at` and `contains`. No entity moves again, so a `Ref` to an entity stays
///   valid while the vector holds the entity.
///
/// `size()` and `empty()` can be called in both phases. Access to an entity is always `const`, so an entity does not
/// change after it is added.
///
/// A default-constructed vector is empty and not frozen. A vector cannot be copied, because a copy would hold a second
/// object for every entity. Moving a vector does not move its entities, so a `Ref` to an entity stays valid.
/// Assigning to a vector destroys the entities that the vector held, and every `Ref` to those entities becomes
/// invalid.
///
/// Takes: `T` must satisfy `entity`, and its primary key must not lead back to `T`.
template <typename T>
class EntityVector {
  static_assert(entity<T>,
                "EntityVector<T> holds each entity once by its primary key: give T a member `primaryKey()`, or a "
                "free `primaryKey(const T&)` beside it, returning what identifies it - an id, a code, or a std::tuple "
                "of them.");
  static_assert(!internal::keyReachesItself<T>(), DREAMOR_INTERNAL_KEY_REACHES_ITSELF_MESSAGE);

 public:
  using value_type     = T;
  using const_iterator = std::vector<T>::const_iterator;
  using iterator       = const_iterator;
  using key_type       = std::remove_cvref_t<detail::PrimaryKeyResult<T>>;
  using size_type      = std::size_t;

  EntityVector()                               = default;
  EntityVector(const EntityVector&)            = delete;
  EntityVector& operator=(const EntityVector&) = delete;
  EntityVector(EntityVector&&) noexcept        = default;
  EntityVector& operator=(EntityVector&&)      = default;
  ~EntityVector()                              = default;

  /// Reserves storage for at least `count` elements.
  ///
  /// Contract: entity_vector_filling
  void reserve(size_type count) {
    filling("reserve");
    items_.reserve(count);
  }

  /// Inserts `entity` unless an entity with the same primary key is already held.
  ///
  /// Returns: True if `entity` was inserted; false otherwise, and the held entity is left unchanged.
  /// Contract: entity_vector_filling
  bool insert(const T& entity) { return emplace(entity); }

  /// @overload
  /// Inserts `entity` by moving it, unless an entity with the same primary key is already held.
  ///
  /// `entity` is moved from even if it is not inserted.
  ///
  /// Returns: True if `entity` was inserted; false otherwise, and the held entity is left unchanged.
  /// Contract: entity_vector_filling
  bool insert(T&& entity) { return emplace(std::move(entity)); }

  /// Constructs an entity from `args` and keeps it unless an entity with the same primary key is already held.
  ///
  /// The new entity is constructed at the end of the vector before its primary key is checked. If the primary key is
  /// already held, the new entity is destroyed and the held entity is left unchanged. If adding the primary key to the
  /// index throws an exception, the new entity is destroyed before the exception propagates.
  ///
  /// Returns: True if the new entity was kept; false otherwise.
  /// Contract: entity_vector_filling
  template <typename... Args>
  bool emplace(Args&&... args) {
    filling("emplace");
    items_.emplace_back(std::forward<Args>(args)...);
    const std::size_t entry = items_.size() - 1;
    std::size_t held        = Index::kNotFound;
    try {
      held = index_.insert(detail::indexKeyOf(items_.back()), entry, keyAt());
    } catch (...) {
      items_.pop_back();
      throw;
    }
    if (held != Index::kNotFound) {
      items_.pop_back();
      return false;
    }
    return true;
  }

  /// Sorts the elements by primary key in ascending order, with `std::less<>`.
  ///
  /// In the reading phase, iteration follows this order, whatever order the elements were inserted in.
  ///
  /// If the memory for the new index cannot be allocated, the vector is left as it was. If a comparison or a primary
  /// key throws an exception while the elements are sorted, the vector is left empty.
  ///
  /// Takes: The primary key of `T` must be comparable with `<`.
  /// Returns: A reference to this vector.
  /// Contract: entity_vector_filling
  EntityVector& sort() {
    return sortBy(
        [](const T& a, const T& b) { return std::less<>{}(internal::primaryKeyOf(a), internal::primaryKeyOf(b)); });
  }

  /// @overload
  /// Sorts the elements by the comparison `comp`.
  ///
  /// In the reading phase, iteration follows this order, whatever order the elements were inserted in. Elements that
  /// `comp` considers equivalent are left in an unspecified order.
  ///
  /// If the memory for the new index cannot be allocated, the vector is left as it was. If a comparison or a primary
  /// key throws an exception while the elements are sorted, the vector is left empty.
  ///
  /// Takes: `comp` must be a strict weak ordering on two `const T&`, as for `std::sort`.
  /// Returns: A reference to this vector.
  /// Contract: entity_vector_filling
  template <typename Compare>
  EntityVector& sort(Compare comp) {
    return sortBy(std::move(comp));
  }

  /// Ends the filling phase and starts the reading phase.
  ///
  /// After this call, the functions of the filling phase must not be called, so no entity moves again. A second call
  /// to `freeze()` has no effect. A frozen vector cannot be unfrozen.
  void freeze() noexcept { frozen_ = true; }

  /// Checks whether the vector is frozen.
  ///
  /// Returns: True if `freeze()` was called on the vector; false otherwise.
  [[nodiscard]] bool frozen() const noexcept { return frozen_; }

  /// Returns the number of elements.
  ///
  /// `size()` can be called in both the filling phase and the reading phase.
  [[nodiscard]] size_type size() const noexcept { return items_.size(); }

  /// Checks whether the vector holds no elements.
  ///
  /// `empty()` can be called in both the filling phase and the reading phase.
  ///
  /// Returns: True if the vector holds no elements; false otherwise.
  [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

  /// Returns an iterator to the first element.
  ///
  /// The elements are in the order in which they were inserted, or in the order of the last `sort`.
  ///
  /// Contract: entity_vector_reading
  [[nodiscard]] const_iterator begin() const {
    reading("iteration");
    return items_.cbegin();
  }

  /// Returns an iterator past the last element.
  ///
  /// Contract: entity_vector_reading
  [[nodiscard]] const_iterator end() const {
    reading("iteration");
    return items_.cend();
  }

  /// Returns the same iterator as `begin()`.
  ///
  /// Contract: entity_vector_reading
  [[nodiscard]] const_iterator cbegin() const { return begin(); }

  /// Returns the same iterator as `end()`.
  ///
  /// Contract: entity_vector_reading
  [[nodiscard]] const_iterator cend() const { return end(); }

  /// Returns the element at `position`.
  ///
  /// If `position` is not less than `size()`, the behavior is undefined.
  ///
  /// Contract: entity_vector_reading
  [[nodiscard]] const T& operator[](size_type position) const {
    reading("operator[]");
    return items_[position];
  }

  /// Returns the first element.
  ///
  /// If the vector is empty, the behavior is undefined.
  ///
  /// Contract: entity_vector_reading
  [[nodiscard]] const T& front() const {
    reading("front");
    return items_.front();
  }

  /// Returns the last element.
  ///
  /// If the vector is empty, the behavior is undefined.
  ///
  /// Contract: entity_vector_reading
  [[nodiscard]] const T& back() const {
    reading("back");
    return items_.back();
  }

  /// Returns a `Ref` to `entity`, an element of the vector.
  ///
  /// `ref` makes the `Ref` from the element's place in the vector, with no lookup of its primary key. Use it where the
  /// element is already at hand, such as in a loop over the vector; to go by the primary key, use `at` or `find`.
  ///
  /// Contract: entity_vector_reading, entity_vector_element
  [[nodiscard]] Ref<T> ref(const T& entity) const {
    reading("ref");
    checkElement(entity);
    return internal::RefAccess::make(&entity);
  }

  /// Returns the elements as `Ref`s, in the order of iteration.
  ///
  /// The range is a view of the vector: it refers to the vector and is valid while the vector exists. Its element at
  /// each position is a `Ref` to the vector's element at that position.
  ///
  /// Contract: entity_vector_reading
  [[nodiscard]] auto refs() const {
    reading("refs");
    return items_ | std::views::transform([](const T& entity) { return internal::RefAccess::make(&entity); });
  }

  /// Returns a `Ref` to the entity whose primary key equals `key`.
  ///
  /// Returns: An empty `Ref` if no entity has the key.
  /// Contract: entity_vector_reading
  [[nodiscard]] Ref<T> find(const key_type& key) const {
    reading("find");
    const std::size_t found = index_.find(asIndexKey(key), keyAt());
    return found == Index::kNotFound ? Ref<T>{} : internal::RefAccess::make(&items_[found]);
  }

  /// @overload
  /// Returns a `Ref` to the entity whose `std::string` primary key equals `key`, without constructing a `std::string`.
  ///
  /// Takes: The primary key of `T` must be a `std::string`. `key` may be any value that converts to
  /// `std::string_view`, such as a string literal, except a `std::string`, which the other overload takes.
  /// Returns: An empty `Ref` if no entity has the key.
  /// Contract: entity_vector_reading
  template <typename Text>
    requires detail::text_key_probe<key_type, Text>
  [[nodiscard]] Ref<T> find(const Text& key) const {
    reading("find");
    const std::size_t found = index_.findEquivalent(std::tuple<std::string_view>{key}, keyAt());
    return found == Index::kNotFound ? Ref<T>{} : internal::RefAccess::make(&items_[found]);
  }

  /// Returns a `Ref` to the entity whose primary key equals `key`.
  ///
  /// If the key's type can be written in a name, the message of the `std::out_of_range` exception contains the key.
  ///
  /// Throws: `std::out_of_range` if no entity has the key.
  /// Contract: entity_vector_reading
  [[nodiscard]] Ref<T> at(const key_type& key) const { return foundOrThrow(find(key), key); }

  /// @overload
  /// Returns a `Ref` to the entity whose `std::string` primary key equals `key`, without constructing a `std::string`.
  ///
  /// If the key can be written in a name, the message of the `std::out_of_range` exception contains the key.
  ///
  /// Takes: The primary key of `T` must be a `std::string`. `key` may be any value that converts to
  /// `std::string_view`, such as a string literal, except a `std::string`, which the other overload takes.
  /// Throws: `std::out_of_range` if no entity has the key.
  /// Contract: entity_vector_reading
  template <typename Text>
    requires detail::text_key_probe<key_type, Text>
  [[nodiscard]] Ref<T> at(const Text& key) const {
    return foundOrThrow(find(key), key);
  }

  /// Checks whether an entity of the vector has the primary key `key`.
  ///
  /// Returns: True if an entity has the key; false otherwise.
  /// Contract: entity_vector_reading
  [[nodiscard]] bool contains(const key_type& key) const { return static_cast<bool>(find(key)); }

  /// @overload
  /// Checks whether an entity of the vector has a `std::string` primary key equal to `key`, without constructing a
  /// `std::string`.
  ///
  /// Takes: The primary key of `T` must be a `std::string`. `key` may be any value that converts to
  /// `std::string_view`, such as a string literal, except a `std::string`, which the other overload takes.
  /// Returns: True if an entity has the key; false otherwise.
  /// Contract: entity_vector_reading
  template <typename Text>
    requires detail::text_key_probe<key_type, Text>
  [[nodiscard]] bool contains(const Text& key) const {
    return static_cast<bool>(find(key));
  }

 private:
  using IndexKey = decltype(detail::indexKeyOf(std::declval<const T&>()));
  using Index    = ::dreamor::internal::FlatIndex<IndexKey>;

  /// @contract entity_vector_filling: An `EntityVector` is not frozen when `reserve`, `insert`, `emplace` or `sort` is
  /// called on it.

  /// @contract entity_vector_reading: An `EntityVector` is frozen before it is iterated, indexed by position, or used
  /// for a lookup.

  /// @contract entity_vector_element: The entity that `EntityVector::ref` receives is an element of that vector, not
  /// a copy of one or an element of another vector.

  // A debug build checks that each call comes in its phase: filling() before freeze(), reading() after it.
  void filling([[maybe_unused]] const char* what) const {
#ifndef NDEBUG
    if (frozen_) {
      DREAMOR_DEBUG_FATAL("EntityVector::{} after freeze() - a broken contract: a frozen EntityVector is read only.",
                          what);
    }
#endif
  }

  void reading([[maybe_unused]] const char* what) const {
#ifndef NDEBUG
    if (!frozen_) {
      DREAMOR_DEBUG_FATAL("EntityVector::{} before freeze() - a broken contract: fill it, freeze it, then read it.",
                          what);
    }
#endif
  }

  // A debug build checks entity_vector_element by the entity's address, which lies within the elements' array.
  void checkElement([[maybe_unused]] const T& entity) const {
#ifndef NDEBUG
    const std::less<const T*> before;
    if (before(&entity, items_.data()) || !before(&entity, items_.data() + items_.size())) {
      DREAMOR_DEBUG_FATAL(
          "EntityVector::ref of an entity that is not one of its elements - a broken contract: ref takes an element "
          "of the vector, as a loop over it gives one.");
    }
#endif
  }

  template <typename Key>
  [[nodiscard]] static Ref<T> foundOrThrow(Ref<T> element, const Key& key) {
    if (!element) {
      throw std::out_of_range("EntityVector::at: no element has the key " + internal::keyText(key) + ".");
    }
    return element;
  }

  template <typename Compare>
  EntityVector& sortBy(Compare comp) {
    filling("sort");
    // The new index is allocated for every element before the elements move, and an index with that room allocates
    // nothing as it is filled. So a std::bad_alloc from the index leaves the vector as it was. If the comparison or a
    // primary key throws after the elements have moved, their order no longer matches either index, and the vector is
    // left empty: valid, with an index that matches it.
    Index sorted{items_.size()};
    try {
      std::ranges::sort(items_, comp);
      static_cast<void>(sorted.insertAll(0, items_.size(), keyAt()));
    } catch (...) {
      items_.clear();
      index_ = Index{};
      throw;
    }
    index_ = std::move(sorted);
    return *this;
  }

  [[nodiscard]] static IndexKey asIndexKey(const key_type& key) {
    if constexpr (detail::IsStdTuple<key_type>::value) {
      return key;
    } else {
      return IndexKey{key};
    }
  }

  [[nodiscard]] auto keyAt() const {
    return [this](std::size_t entry) { return detail::indexKeyOf(items_[entry]); };
  }

  std::vector<T> items_;
  Index index_;
  bool frozen_ = false;  // present in every build, so that debug and release builds have the same class layout
};

}  // namespace dreamor
