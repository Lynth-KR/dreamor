// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// File: entity_vector.hpp
// Naming convention
//    1.   Type: Pascal Case (i.e., MyType)
//    2.   Variables: snake case (i.e., my_variable)
//    2-1. Member Variables: snake case with trailing underscore (i.e., my_variable_)
//    3.   Functions: camel Case (i.e., myFunction())
//    3.   Constants: kPascal Case (i.e., kMyConstant)
//    4.   Macros: Screaming Snake Case (i.e., MY_MACRO)
//
// A container of entities that holds each one once - by its primary key, not its address. A
// set keyed by entity pointers rests on that (index_set.hpp: an entity is one object, in one
// container): two objects for one machine would be two machines to every set built on them.
//
// The primary key is what the entity says identifies it, through a free function found by ADL,
// next to the type - as keyName() is:
//
//     inline const std::string& primaryKey(const Machine& m) { return m.code; }
//     inline auto primaryKey(const Route& r) { return std::tuple{r.plant, r.code}; }   // composite
//
//     dreamor::EntityVector<Machine> machines;
//     machines.reserve(rows.size());
//     for (const auto& r : rows) { machines.emplace(r.code, r.capacity); }   // a key held already stays out
//     machines.sort().freeze();                                   // by primary key, optional; then read only
//     makeSet<cMachine, cPeriod>(machines, periods)            // a container source, as any
//
// It is filled, then frozen, then read - the order a model's data goes in:
//   - Before freeze(): reserve, insert / emplace and sort. insert / emplace return {the element
//     with that key, whether it went in}, as std::set's do; the element lasts as a std::vector's
//     does, until a growth past the capacity reserved or a sort.
//   - sort() puts the elements in primary-key order (std::less), sort(comp) in comp's, as
//     std::sort does: the order every set built on them then follows, whatever order the data
//     came in.
//   - After freeze(): reading - iteration, [], find, a set source - and modify. Elements never
//     move again, so a pointer taken into the model stays the entity's; there is no erase. Each
//     side of freeze() is a contract a debug build checks.
//   - Access is const. An element changes only through modify(entity, fn), as in
//     Boost.MultiIndex: fn gets it mutable, and its primary key must come out the same. Meant
//     for writing results back once the model is built - sets are snapshots, and a predicate
//     that read the old value is not run again. The usual home for results is a ValueTable.
//   - Copying is deleted: a copy is a second object for every entity. Moving keeps them where
//     they are.
#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "dreamor/general/flat_index.hpp"
#include "dreamor/logger/logger_light.hpp"

namespace dreamor {

namespace internal {

template <typename T>
struct IsStdTuple : std::false_type {};
template <typename... Ts>
struct IsStdTuple<std::tuple<Ts...>> : std::true_type {};

// What primaryKey() returns, by reference or by value.
template <typename T>
using PrimaryKeyResult = decltype(primaryKey(std::declval<const T&>()));  // found via ADL

// The key as the index hashes it: a composite key is a tuple already, any other is one.
template <typename T>
[[nodiscard]] auto indexKeyOf(const T& entity) {
  using Result = PrimaryKeyResult<T>;
  if constexpr (IsStdTuple<std::remove_cvref_t<Result>>::value) {
    return primaryKey(entity);
  } else {
    return std::tuple<Result>{primaryKey(entity)};  // a reference stays one: no copy of a string
  }
}

}  // namespace internal

// An entity: something with a primary key, by `primaryKey(const T&)` found next to T.
template <typename T>
concept Entity = requires(const T& entity) {
  { primaryKey(entity) };  // found via ADL
  requires std::equality_comparable<std::remove_cvref_t<decltype(primaryKey(entity))>>;
};

template <typename T>
class EntityVector {
  static_assert(Entity<T>,
                "EntityVector<T> holds each entity once by its primary key: define `primaryKey(const T&)` next to T, "
                "returning what identifies it - an id, a code, or a std::tuple of them.");

 public:
  using value_type     = T;
  using const_iterator = std::vector<T>::const_iterator;
  using iterator       = const_iterator;
  using key_type       = std::remove_cvref_t<internal::PrimaryKeyResult<T>>;  // what primaryKey() returns
  using size_type      = std::size_t;

  EntityVector()                               = default;
  EntityVector(const EntityVector&)            = delete;
  EntityVector& operator=(const EntityVector&) = delete;
  EntityVector(EntityVector&&) noexcept        = default;
  EntityVector& operator=(EntityVector&&)      = default;
  ~EntityVector()                              = default;

  // Filling: before freeze().

  void reserve(size_type count) {
    filling("reserve");
    items_.reserve(count);
  }

  // {the element with this key, whether it went in}: false when the key was already held, and
  // the element stays as it was. The element lasts until a growth past the capacity reserved, or
  // a sort, as a std::vector's does.
  std::pair<const T*, bool> insert(const T& entity) { return emplace(entity); }
  std::pair<const T*, bool> insert(T&& entity) { return emplace(std::move(entity)); }

  // Builds the element in place, then keeps it only if its key is new. If indexing throws, the
  // element goes again and the container is as it was.
  template <typename... Args>
  std::pair<const T*, bool> emplace(Args&&... args) {
    filling("emplace");
    items_.emplace_back(std::forward<Args>(args)...);
    const std::size_t entry = items_.size() - 1;
    std::size_t held        = Index::kNotFound;
    try {
      held = index_.insert(internal::indexKeyOf(items_.back()), entry, keyAt());
    } catch (...) {
      items_.pop_back();
      throw;
    }
    if (held != Index::kNotFound) {
      items_.pop_back();
      return {&items_[held], false};
    }
    return {&items_[entry], true};
  }

  // The elements in primary-key order (std::less on what primaryKey() returns), or in comp's -
  // comp(a, b) true when a goes before b, as std::sort takes it. Elements of equal keys cannot
  // be: a key is held once.
  EntityVector& sort() {
    return sortBy([](const T& a, const T& b) { return std::less<>{}(primaryKey(a), primaryKey(b)); });
  }
  template <typename Compare>
  EntityVector& sort(Compare comp) {
    return sortBy(std::move(comp));
  }

  // Done filling: from here on the elements are read, and stay where they are.
  void freeze() noexcept { frozen_ = true; }
  [[nodiscard]] bool frozen() const noexcept { return frozen_; }

  [[nodiscard]] size_type size() const noexcept { return items_.size(); }
  [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

  // Reading: after freeze().

  [[nodiscard]] const_iterator begin() const {
    reading("iteration");
    return items_.cbegin();
  }
  [[nodiscard]] const_iterator end() const {
    reading("iteration");
    return items_.cend();
  }
  [[nodiscard]] const_iterator cbegin() const { return begin(); }
  [[nodiscard]] const_iterator cend() const { return end(); }
  [[nodiscard]] const T& operator[](size_type position) const {
    reading("operator[]");
    return items_[position];
  }
  [[nodiscard]] const T& at(size_type position) const {
    reading("at");
    return items_.at(position);
  }
  [[nodiscard]] const T& front() const {
    reading("front");
    return items_.front();
  }
  [[nodiscard]] const T& back() const {
    reading("back");
    return items_.back();
  }

  // The element with this key, by its address, or null.
  [[nodiscard]] const T* find(const key_type& key) const {
    reading("find");
    const std::size_t found = index_.find(asIndexKey(key), keyAt());
    return found == Index::kNotFound ? nullptr : &items_[found];
  }
  [[nodiscard]] bool contains(const key_type& key) const { return find(key) != nullptr; }

  // Changes one element: fn(T&). The element must be one of this container's, and its primary
  // key must not change - both preconditions, checked by a debug build.
  template <typename Fn>
  void modify(const T* entity, Fn fn) {
    reading("modify");
#ifndef NDEBUG
    const std::size_t found =
        entity == nullptr ? Index::kNotFound : index_.find(internal::indexKeyOf(*entity), keyAt());
    if (found == Index::kNotFound || &items_[found] != entity) {
      DREAMOR_DEBUG_FATAL("EntityVector::modify: the entity is not one of this container's - a broken contract.");
    }
    const key_type before = primaryKey(*entity);
#endif
    // The element is the container's own and not const: const is only how the container hands it out.
    fn(const_cast<T&>(*entity));  // NOLINT(cppcoreguidelines-pro-type-const-cast): see above
#ifndef NDEBUG
    if (!(primaryKey(*entity) == before)) {
      DREAMOR_DEBUG_FATAL(
          "EntityVector::modify: fn changed the entity's primary key - a broken contract: a key "
          "identifies its entity for good.");
    }
#endif
  }

 private:
  using IndexKey = decltype(internal::indexKeyOf(std::declval<const T&>()));
  using Index    = ::dreamor::general::FlatIndex<IndexKey>;

  // The two sides of freeze(), checked by a debug build.
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

  template <typename Compare>
  EntityVector& sortBy(Compare comp) {
    filling("sort");
    std::ranges::sort(items_, comp);
    Index sorted;  // the entries are the new positions
    static_cast<void>(sorted.insertAll(0, items_.size(), keyAt()));
    index_ = std::move(sorted);
    return *this;
  }

  [[nodiscard]] static IndexKey asIndexKey(const key_type& key) {
    if constexpr (internal::IsStdTuple<key_type>::value) {
      return key;
    } else {
      return IndexKey{key};
    }
  }

  [[nodiscard]] auto keyAt() const {
    return [this](std::size_t entry) { return internal::indexKeyOf(items_[entry]); };
  }

  std::vector<T> items_;
  Index index_;
  bool frozen_ = false;  // in every build, so that a debug and a release build agree on the layout
};

}  // namespace dreamor
