// SPDX-License-Identifier: Apache-2.0
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
//     const auto [at, inserted] = machines.emplace("M1", 40.0);   // inserted == false: M1 was there
//     makeSet<MachineCol, PeriodCol>(machines, periods)            // a container source, as any
//
// It reads like a std::vector and inserts like a std::set:
//   - insert / emplace return {the element with that key, whether it went in}, as std::set's
//     do. Ignoring the bool is the caller's business.
//   - Elements never move: they live in a std::deque, which keeps references valid as it grows,
//     so a pointer taken into the model stays the entity's. There is no erase, for the same
//     reason.
//   - Access is const. An element changes only through modify(entity, fn), as in
//     Boost.MultiIndex: fn gets it mutable, and its primary key must come out the same. Meant
//     for writing results back once the model is built - sets are snapshots, and a predicate
//     that read the old value is not run again. The usual home for results is a ValueTable.
//   - Copying is deleted: a copy is a second object for every entity. Moving keeps them where
//     they are.
#pragma once

#include <concepts>
#include <cstddef>
#include <deque>
#include <tuple>
#include <type_traits>
#include <utility>

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
  using const_iterator = std::deque<T>::const_iterator;
  using iterator       = const_iterator;
  using key_type       = std::remove_cvref_t<internal::PrimaryKeyResult<T>>;  // what primaryKey() returns
  using size_type      = std::size_t;

  EntityVector()                               = default;
  EntityVector(const EntityVector&)            = delete;
  EntityVector& operator=(const EntityVector&) = delete;
  EntityVector(EntityVector&&) noexcept        = default;
  EntityVector& operator=(EntityVector&&)      = default;
  ~EntityVector()                              = default;

  [[nodiscard]] const_iterator begin() const noexcept { return items_.cbegin(); }
  [[nodiscard]] const_iterator end() const noexcept { return items_.cend(); }
  [[nodiscard]] const_iterator cbegin() const noexcept { return items_.cbegin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return items_.cend(); }
  [[nodiscard]] size_type size() const noexcept { return items_.size(); }
  [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
  [[nodiscard]] const T& operator[](size_type position) const { return items_[position]; }
  [[nodiscard]] const T& at(size_type position) const { return items_.at(position); }
  [[nodiscard]] const T& front() const { return items_.front(); }
  [[nodiscard]] const T& back() const { return items_.back(); }

  // {the element with this key, whether it went in}: false when the key was already held,
  // and the element stays as it was.
  std::pair<const_iterator, bool> insert(const T& entity) { return emplace(entity); }
  std::pair<const_iterator, bool> insert(T&& entity) { return emplace(std::move(entity)); }

  // Builds the element in place, then keeps it only if its key is new. If indexing throws, the
  // element goes again and the container is as it was.
  template <typename... Args>
  std::pair<const_iterator, bool> emplace(Args&&... args) {
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
      return {positionOf(held), false};
    }
    return {positionOf(entry), true};
  }

  // The element with this key, or end().
  [[nodiscard]] const_iterator find(const key_type& key) const {
    const std::size_t found = index_.find(asIndexKey(key), keyAt());
    return found == Index::kNotFound ? end() : positionOf(found);
  }
  [[nodiscard]] bool contains(const key_type& key) const { return find(key) != end(); }

  // Changes one element: fn(T&). The element must be one of this container's, and its primary
  // key must not change - both preconditions, checked by a debug build.
  template <typename Fn>
  void modify(const T* entity, Fn fn) {
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

  [[nodiscard]] const_iterator positionOf(std::size_t position) const {
    return items_.cbegin() + static_cast<std::deque<T>::difference_type>(position);
  }

  std::deque<T> items_;
  Index index_;
};

}  // namespace dreamor
