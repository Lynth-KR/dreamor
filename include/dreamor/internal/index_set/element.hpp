// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// What a chain needs to know about a source: its kind, whether the set's rows refer into it, and how it is held.
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "dreamor/internal/index_set/column.hpp"

namespace dreamor {
template <typename T>
class EntityVector;  // Only the name is needed here. The class is in dreamor/entity_vector.hpp.
}  // namespace dreamor

namespace dreamor::internal::inline index_set {

// Whether a source is an EntityVector, or a ref_view of an EntityVector.
template <typename Range>
struct IsEntityVectorRange : std::false_type {};

template <typename T>
struct IsEntityVectorRange<EntityVector<T>> : std::true_type {};

template <typename T>
struct IsEntityVectorRange<std::ranges::ref_view<const EntityVector<T>>> : std::true_type {};

template <typename T>
struct IsEntityVectorRange<std::ranges::ref_view<EntityVector<T>>> : std::true_type {};

// Whether a range may fill the column Column. An entity column takes the entities of their EntityVector, or Refs:
// bare, in a column, or in a row of one column. A Ref refers only into a frozen EntityVector, so entities from any
// other place have nothing for a Ref to refer to. A copy of the EntityVector, returned by value, is such another place.
// A value column takes any range.
template <typename C>
concept ref_column = column<C> && ref<typename C::value_type>;

template <typename Range, typename Column>
[[nodiscard]] consteval bool refsFromTheirVector() {
  using Bare = std::remove_cvref_t<Range>;
  if constexpr (!ref_column<Column> || !std::ranges::range<Bare>) {
    return true;
  } else {
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Bare>>;
    if constexpr (ref<Element>) {
      return true;
    } else if constexpr (column<Element>) {
      return ref<typename Element::value_type>;
    } else if constexpr (OneColumnRow<Element>::value) {
      return ref<typename OneColumnRow<Element>::column_type::value_type>;
    } else {
      return IsEntityVectorRange<Bare>::value && (std::is_reference_v<Range> || std::ranges::view<Bare>);
    }
  }
}

// The message for an entity column filled from anything but its EntityVector or Refs. A chain and sumExpr share it.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_REFS_FROM_THEIR_VECTOR_MESSAGE                                                          \
  "A Ref column takes its entities from their EntityVector - index_set::relational::from<cMachine>(machines) - " \
  "or the Refs it gave (at, find): entities or pointers from any other container have no frozen entity to refer to."

// Reads a plain container as rows of one column. No vector of tuples is built: each element is wrapped in the column
// as it is visited.
template <typename Column, typename Rows>
[[nodiscard]] auto namedAsOneColumn(Rows&& rows) {
  return std::views::all(std::forward<Rows>(rows)) |
         std::views::transform([](auto&& element) { return std::tuple{toColumn<Column>(element)}; });
}

// The three kinds of source:
//   - kContainer fills one column with the same range for every row so far.
//   - kDependent, a lateral source, fills one column with the values that it returns for each row so far. If it
//     returns a reference to a stored list, no allocation is made for each row.
//   - kSpread, a set or a range of rows, fills as many columns as its rows have.
enum class SourceKind : std::uint8_t { kContainer, kDependent, kSpread };

template <typename T>
struct IsTuple : std::false_type {};

template <typename... Ts>
struct IsTuple<std::tuple<Ts...>> : std::true_type {};

// Whether a range's elements are rows of one column that holds the value type of Column. Such a range fills Column
// with the values of its rows, under the name Column. A set of one column passed to cross for another column, or
// returned by a lateral source, is such a range.
template <typename Range, typename Column>
[[nodiscard]] consteval bool oneColumnRowsFor() {
  using Bare = std::remove_cvref_t<Range>;
  if constexpr (!column<Column> || !std::ranges::range<Bare&>) {
    return false;
  } else {
    using Element = std::remove_cvref_t<std::ranges::range_value_t<Bare&>>;
    if constexpr (OneColumnRow<Element>::value) {
      return std::same_as<typename OneColumnRow<Element>::column_type::value_type, typename Column::value_type>;
    } else {
      return false;
    }
  }
}

// Turns a row of one column into the value that its column holds. If the range keeps its rows, the result is a
// reference into the row. If the range makes each row as it is read, as a transform view does, the row is gone after
// the call, so the value is moved out of it.
struct FirstColumn {
  template <typename Row>
  [[nodiscard]] decltype(auto) operator()(Row&& row) const {
    if constexpr (std::is_lvalue_reference_v<Row>) {
      return std::get<0>(row);
    } else {
      return std::tuple_element_t<0, std::remove_cvref_t<Row>>{std::get<0>(std::forward<Row>(row))};
    }
  }
};

// Returns the kind of a source. A range of tuples is read as kSpread: rows of a set, each element a column.
template <typename Source>
[[nodiscard]] consteval SourceKind sourceKindOf() {
  if constexpr (std::ranges::forward_range<Source&>) {
    return IsTuple<std::ranges::range_value_t<Source>>::value ? SourceKind::kSpread : SourceKind::kContainer;
  } else {
    return SourceKind::kDependent;
  }
}

// Whether a row keeps the address of a source's elements, so that the source must outlive the set. Only an
// EntityVector is such a source, because its Refs refer to its entities. Rows of any other source copy what they
// hold, so any other source may be a temporary.
template <typename Source>
[[nodiscard]] consteval bool rowsReferToElements() {
  return IsEntityVectorRange<std::remove_cvref_t<Source>>::value;
}

// Whether a column of std::string_view, filled from a range of type R, would view text that lives inside the range's
// elements. That is the case if the elements are std::strings. The elements of such a range must outlive the set, as
// the entities of an EntityVector must.
template <typename C>
concept text_column = column<C> && std::same_as<typename C::value_type, std::string_view>;

template <typename R, typename Column>
[[nodiscard]] consteval bool rowsShowElementText() {
  using Bare = std::remove_cvref_t<R>;
  if constexpr (!text_column<Column> || !std::ranges::range<Bare>) {
    return false;
  } else {
    return std::is_same_v<std::remove_cvref_t<std::ranges::range_value_t<Bare>>, std::string>;
  }
}

// Whether a range passed as R, a reference or a value, keeps its elements after the call. That is the case if the
// range holds its elements instead of making them, and R is an lvalue reference or a borrowed range.
// If R is not a range, the answer is true, and another check reports the mistake. That case needs its own branch
// because GCC 16 instantiates this function even where the caller's || already has its answer.
template <typename R>
[[nodiscard]] consteval bool keepsElements() {
  using Bare = std::remove_cvref_t<R>;
  if constexpr (!std::ranges::range<Bare&>) {
    return true;
  } else {
    return std::is_lvalue_reference_v<std::ranges::range_reference_t<Bare&>> &&
           (std::is_lvalue_reference_v<R> || std::ranges::borrowed_range<Bare>);
  }
}

// The message for a column of std::string_view whose source's strings would not outlive the set.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): a static_assert message has to be a string literal
#define DREAMOR_INTERNAL_TEXT_OUTLIVES_MESSAGE                                                                \
  "A text column shows the text inside its source's std::strings, so they have to outlive the set: hand a "   \
  "named container or a view of one, and let a list per row (lateral<C>(f), in<C>(f)) return a reference or " \
  "a std::span - not a temporary, a copy, nor a range that makes its strings."

// Whether a source is a view whose elements come by value, such as an interval or a transform view that makes them.
// Such a view holds nothing that a row could point into, so it may be passed to a chain as a temporary.
template <typename Source>
[[nodiscard]] consteval bool yieldsValues() {
  if constexpr (std::ranges::view<Source>) {
    return !std::is_reference_v<std::ranges::range_reference_t<Source>>;
  } else {
    return false;
  }
}

// A view of the array behind values written in braces and passed to from, cross or in. The array lasts until the end
// of the statement that writes it, as a temporary container does. The chain holds this view, not the
// std::initializer_list, because the std::initializer_list is a parameter of the step and may be destroyed when the
// step returns.
template <typename Value>
class BracedValues : public std::ranges::view_interface<BracedValues<Value>> {
 public:
  BracedValues() = default;

  // Takes the list's array as its begin() and end() return it. There is no constructor from the list, because such a
  // constructor would let `BracedValues<int> b = {1, 2};` keep a view of an array that is destroyed at the end of
  // that line.
  BracedValues(const Value* first, const Value* last) noexcept : first_{first}, last_{last} {}

  [[nodiscard]] const Value* begin() const noexcept { return first_; }

  [[nodiscard]] const Value* end() const noexcept { return last_; }

  // Returns an unsigned count, as std::initializer_list::size does. The size() of view_interface would return the
  // signed difference of the pointers.
  [[nodiscard]] std::size_t size() const noexcept { return static_cast<std::size_t>(last_ - first_); }

 private:
  const Value* first_ = nullptr;
  const Value* last_  = nullptr;
};

// A view of a temporary container that a condition of set-builder notation reads (set_builder.hpp). It is held by
// value, as any view is, and it lasts until the end of the statement that writes it, as the temporary container
// does.
template <typename View>
class StatementView : public std::ranges::view_interface<StatementView<View>> {
 public:
  StatementView() = default;

  explicit StatementView(View view) : view_{std::move(view)} {}

  [[nodiscard]] auto begin() const { return std::ranges::begin(view_); }

  [[nodiscard]] auto end() const { return std::ranges::end(view_); }

  [[nodiscard]] auto size() const
    requires std::ranges::sized_range<const View>
  {
    return std::ranges::size(view_);
  }

 private:
  View view_;
};

// Whether a view lasts only until the end of the statement that writes it. Values in braces and a StatementView do.
template <typename Range>
struct IsStatementScoped : std::false_type {};

template <typename Value>
struct IsStatementScoped<BracedValues<Value>> : std::true_type {};

template <typename View>
struct IsStatementScoped<StatementView<View>> : std::true_type {};

// Holds a range as a view of the caller's container, and a callable by value.
template <typename Source>
[[nodiscard]] auto holdSource(Source&& source) {
  using Bare = std::remove_reference_t<Source>;
  if constexpr (std::ranges::forward_range<Bare&>) {
    return std::views::all(source);
  } else {
    return std::forward<Source>(source);
  }
}

// What a callable returns when it is called with its own columns. `invocable` is false where invoke_result_t would
// be a hard error.
template <typename Callable, typename Cols>
struct ReturnTypeOf {
  using type = void;

  static constexpr bool invocable = false;
};

template <typename Callable, typename... Cols>
  requires std::invocable<Callable, Cols...>
struct ReturnTypeOf<Callable, std::tuple<Cols...>> {
  using type = std::invoke_result_t<Callable, Cols...>;

  static constexpr bool invocable = true;
};

}  // namespace dreamor::internal::inline index_set
