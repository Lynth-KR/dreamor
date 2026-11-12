// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// A column of plain values (DREAMOR_VALUE_COLUMN): it holds an integer, an enum, a char, a std::string_view or a
// type of the user's with == and std::hash, at that value's size, and converts implicitly to that value alone. A
// column of integers is made explicitly from any integer, compares and orders with any integer, or column of
// integers, by value whatever its signedness, and takes any integer range as a source. Two columns of one value
// type compare as their values; a column hashes as its value. A char names and prints as the character, an enum as its
// number.
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

enum class Colour : std::uint8_t { kRed = 1, kBlue = 2 };

// a type of the user's, which identifies by == and hashes by its std::hash
struct Code {
  std::uint16_t value;
  friend bool operator==(const Code&, const Code&) = default;
};

// one that has a primaryKey() too, as an entity's type would: as a column's value it still hashes by its std::hash
struct Grade {
  std::string code;

  [[nodiscard]] const std::string& primaryKey() const { return code; }

  friend bool operator==(const Grade&, const Grade&) = default;
};

}  // namespace

template <>
struct std::hash<Code> {
  std::size_t operator()(const Code& code) const noexcept { return code.value; }
};

template <>
struct std::hash<Grade> {
  std::size_t operator()(const Grade& grade) const noexcept { return std::hash<std::string>{}(grade.code); }
};

namespace {
enum class Shift : std::uint8_t { kDay, kNight };  // an enum the user gave a formatter
}  // namespace

template <>
struct std::formatter<Shift, char> : std::formatter<std::string_view, char> {
  template <typename FormatContext>
  auto format(Shift shift, FormatContext& context) const {
    return std::formatter<std::string_view, char>::format(shift == Shift::kDay ? "Day" : "Night", context);
  }
};

namespace {

DREAMOR_VALUE_COLUMN(cPeriod, std::uint32_t);
DREAMOR_VALUE_COLUMN(cStart, std::uint32_t);
DREAMOR_VALUE_COLUMN(cShift, std::uint32_t);
DREAMOR_VALUE_COLUMN(cStep, std::int32_t);  // signed: a step may go back
DREAMOR_VALUE_COLUMN(cGrade, std::string_view);
DREAMOR_VALUE_COLUMN(cLetter, char);
DREAMOR_VALUE_COLUMN(cPaint, Colour);
DREAMOR_VALUE_COLUMN(cShift2, Shift);
DREAMOR_VALUE_COLUMN(cCode, Code);
DREAMOR_VALUE_COLUMN(cGradeOf, Grade);
DREAMOR_VALUE_COLUMN(cLabel, std::string);  // a text the column holds, rather than shows
DREAMOR_VALUE_COLUMN(cName, std::string);   // another column of std::string

// a column of std::string is read by reference, not copied
static_assert(std::is_same_v<decltype(std::declval<const cLabel&>().get()), const std::string&>);

template <typename A, typename B>
concept equality_with = requires(const A& a, const B& b) { a == b; };
template <typename A, typename B>
concept ordered_with = requires(const A& a, const B& b) { a < b; };
template <typename A, typename B>
concept addable_with = requires(const A& a, const B& b) { a + b; };

// the value's size, and copied as it is
static_assert(sizeof(cPeriod) == sizeof(std::uint32_t) && std::is_trivially_copyable_v<cPeriod>);
static_assert(sizeof(cGrade) == sizeof(std::string_view) && std::is_trivially_copyable_v<cGrade>);
static_assert(std::totally_ordered<cPeriod> && std::totally_ordered<cGrade> && std::totally_ordered<cPaint>);

// converts implicitly to its value, and through no second conversion of a class's
static_assert(std::is_convertible_v<cPeriod, std::uint32_t> && std::is_convertible_v<cGrade, std::string_view>);
static_assert(std::is_convertible_v<cLetter, char> && std::is_convertible_v<cPaint, Colour>);
static_assert(!std::is_convertible_v<cGrade, std::string>);
// made from its own value; from another integer type, or another column, explicitly
static_assert(std::is_convertible_v<std::uint32_t, cPeriod>);
static_assert(std::is_constructible_v<cPeriod, int> && std::is_constructible_v<cPeriod, std::size_t>);
static_assert(std::is_constructible_v<cPeriod, cStart> && !std::is_convertible_v<cStart, cPeriod>);

// compares, orders and adds with any integer
static_assert(equality_with<cPeriod, int> && ordered_with<cPeriod, int> && addable_with<cPeriod, int>);
static_assert(equality_with<int, cPeriod> && ordered_with<int, cPeriod>);
static_assert(equality_with<cStep, std::size_t> && ordered_with<std::size_t, cStep>);
// two columns of one value type compare as their values: from and to, start and period
static_assert(equality_with<cStart, cPeriod> && ordered_with<cStart, cPeriod> && equality_with<cPeriod, cShift>);
// and two columns of integers of different types, by value
static_assert(equality_with<cStep, cPeriod> && ordered_with<cPeriod, cStep>);

constexpr auto late    = [](cPeriod t) { return t > 1; };
constexpr auto started = [](cStart s, cPeriod t) { return s <= t; };

}  // namespace

int main() try {
  // an integer column against int, unsigned and std::size_t, either way round, by value
  const cPeriod t{3};
  const int three          = 3;
  const int minus_one      = -1;
  const std::uint64_t four = 4;
  const std::size_t size_3 = 3;
  DREAMOR_CHECK(t == three && three == t && t != minus_one && minus_one != t && t == 3U && 3U == t);
  DREAMOR_CHECK(t > minus_one && minus_one < t && t >= three && three <= t && !(t < minus_one));
  DREAMOR_CHECK(t < four && four > t && t == size_3 && size_3 == t && t <= size_3 && size_3 >= t);
  DREAMOR_CHECK((t <=> minus_one) == std::strong_ordering::greater && (t <=> four) == std::strong_ordering::less);
  // a signed column against unsigned values: -1 is below every one of them
  const cStep back{-1};
  DREAMOR_CHECK(back < 0U && back < size_3 && size_3 > back && back != four && back == minus_one);

  // read as its value: t + 1, an index
  const std::vector<std::string> names{"zero", "one", "two", "three"};
  DREAMOR_CHECK(t + 1 == 4 && names[t] == "three" && t.get() == 3U);

  // made from an int or a std::size_t, the column named
  const int two         = 2;
  const std::size_t one = 1;
  DREAMOR_CHECK(cPeriod{two} == 2 && cPeriod{one} == 1 && cPeriod{two}.get() == 2U && cStep{size_3} == 3);

  // two columns of one value type compare as their values; another column's value, written out
  const cStart start{cPeriod{2}};
  DREAMOR_CHECK(start == cPeriod{2} && cPeriod{2} == start && start < t && t > start && cShift{3} == t);
  // two columns of integers of different types compare by value: -1 is below every period
  DREAMOR_CHECK(back < t && t > back && back != t && cStep{3} == t && t == cStep{3} && cStep{3} <= t);
  DREAMOR_CHECK((back <=> cPeriod{0}) == std::strong_ordering::less);

  // hashed as its value, in a standard unordered container
  DREAMOR_CHECK(std::hash<cPeriod>{}(cPeriod{7}) == std::hash<cStart>{}(cStart{7}));
  const std::unordered_set<cPeriod> periods{cPeriod{1}, cPeriod{2}, cPeriod{1}};
  const std::unordered_set<cGrade> grades{cGrade{"A"}, cGrade{"B"}, cGrade{"A"}};
  const std::unordered_set<cLetter> letter_set{cLetter{'a'}, cLetter{'a'}};
  const std::unordered_set<cPaint> paints{cPaint{Colour::kRed}, cPaint{Colour::kBlue}};
  DREAMOR_CHECK(periods.size() == 2 && grades.size() == 2 && letter_set.size() == 1 && paints.size() == 2);
  DREAMOR_CHECK(periods.contains(cPeriod{2}) && !periods.contains(cPeriod{3}));
  // a row of columns keys a standard unordered container: a handle a row
  std::unordered_map<std::tuple<cPeriod, cGrade>, std::size_t> handleOf;
  handleOf.emplace(std::tuple{cPeriod{1}, cGrade{"A"}}, 7);
  handleOf[{cPeriod{2}, cGrade{"B"}}] = 8;
  DREAMOR_CHECK(handleOf.at({cPeriod{1}, cGrade{"A"}}) == 7 && handleOf.size() == 2 &&
                !handleOf.contains({cPeriod{1}, cGrade{"B"}}));

  // a text column compares with a literal and a std::string, either way round
  DREAMOR_CHECK(cGrade{"A"} == "A" && "A" == cGrade{"A"} && cGrade{"A"} < "B" && "B" > cGrade{"A"});
  // NOLINTNEXTLINE(performance-string-view-conversions): the comparison with a std::string is what is checked
  DREAMOR_CHECK(cGrade{"A"} < std::string{"B"} && cGrade{"A"} < cGrade{"B"} && cPaint{Colour::kRed} < Colour::kBlue);

  // a type of the user's identifies by its ==
  DREAMOR_CHECK(cCode{Code{3}} == Code{3} && cCode{Code{3}} == cCode{Code{3}} && cCode{Code{3}} != Code{4});
  const std::unordered_set<cCode> codes{cCode{Code{1}}, cCode{Code{1}}};
  DREAMOR_CHECK(codes.size() == 1);
  const std::vector<Grade> gradeList{{"A"}, {"B"}};
  const auto sGRADES = dreamor::index_set::relational::from<cGradeOf>(gradeList).build();
  DREAMOR_CHECK(sGRADES.size() == 2 && sGRADES.contains(cGradeOf{Grade{"B"}}) &&
                !sGRADES.contains(cGradeOf{Grade{"C"}}));

  // a column of std::string holds its text: made from a std::string a call returns, filled from a temporary
  // container or from views, found by any text, and laid out by a format spec
  const cLabel made{std::string{"made-in-a-call-long-enough"}};
  DREAMOR_CHECK(made == "made-in-a-call-long-enough" && made < cLabel{"z"} &&
                std::format("{:>4}", cLabel{"ab"}) == "  ab");
  const auto sLABELS =
      dreamor::index_set::relational::from<cLabel>(std::vector<std::string>{"press-line-number-one", "B"}).build();
  const std::vector<std::string_view> views{"C", "D"};
  const auto sVIEWED = dreamor::index_set::relational::from<cLabel>(views).build();
  DREAMOR_CHECK(sLABELS.contains("B") && sLABELS.contains(std::string_view{"press-line-number-one"}) &&
                !sLABELS.contains(std::string{"C"}) && sVIEWED.contains("D") && sVIEWED.size() == 2);
  const std::unordered_set<cLabel> labels{made, cLabel{"made-in-a-call-long-enough"}};
  DREAMOR_CHECK(labels.size() == 1);
  // rows of one column made as they are read - a view returning each row - fill another column of their value type,
  // each value taken before its row is gone
  const std::vector<std::size_t> ids{0, 1};
  const auto made_row  = [](std::size_t i) { return std::tuple<cLabel>{std::string(40, static_cast<char>('a' + i))}; };
  const auto sMADE_ROW = dreamor::index_set::relational::from<cName>(ids | std::views::transform(made_row)).build();
  DREAMOR_CHECK(sMADE_ROW.size() == 2 && sMADE_ROW.contains(std::string(40, 'b')));

  // an enum with a formatter of the user's names and prints through it, under a spec too
  DREAMOR_CHECK(std::format("{}|{:>5}", cShift2{Shift::kDay}, cShift2{Shift::kNight}) == "Day|Night" &&
                std::string{dreamor::indexedName("Cover", cShift2{Shift::kNight})} == "Cover_Night");

  // printed as its value: a char as the character, an enum as its number
  DREAMOR_CHECK(
      std::format("{}|{}|{}|{}|{}", cPeriod{5}, cStep{-2}, cGrade{"A"}, cLetter{'x'}, cPaint{Colour::kBlue}) ==
      "5|-2|A|x|2");

  // a column of integers filled from an interval, and from a range of another integer type
  const auto sPERIOD = dreamor::index_set::relational::from<cPeriod>(dreamor::interval::closed(1, 3)).build();
  DREAMOR_CHECK(sPERIOD.size() == 3 && sPERIOD.where(late).size() == 2);
  const std::vector<int> firsts{0, 2};
  const auto sSTART = dreamor::index_set::relational::from<cStart>(firsts).cross(sPERIOD).where(started).build();
  DREAMOR_CHECK(sSTART.size() == 5);  // start 0 with periods 1..3, start 2 with 2..3
  const std::vector<std::size_t> sizes{2, 1};
  const auto sSIZE = dreamor::index_set::relational::from<cPeriod>(sizes).build();
  DREAMOR_CHECK(sSIZE.size() == 2 && std::get<cPeriod>(*sSIZE.begin()) == 2);
  // an interval's end is an integer or a column of one: {u in s..3}
  const auto until_three = [](cStart s) { return dreamor::interval::closed(s, 3); };
  const auto sAFTER      = dreamor::index_set::relational::from<cStart>(firsts).lateral<cPeriod>(until_three).build();
  DREAMOR_CHECK(sAFTER.size() == 6);  // start 0 with periods 0..3, start 2 with 2..3

  // char, enum and text columns, named as their values
  const std::vector<std::string_view> grade_list{"A", "B"};
  const std::vector<char> letters{'x', 'y'};
  const std::vector<Colour> colours{Colour::kRed, Colour::kBlue};
  const auto sMIX =
      dreamor::index_set::relational::from<cGrade>(grade_list).cross<cLetter>(letters).cross<cPaint>(colours).build();
  DREAMOR_CHECK(sMIX.size() == 8);
  const auto& [grade, letter, paint] = *sMIX.begin();
  DREAMOR_CHECK(std::string{dreamor::indexedName("Mix", grade, letter, paint)} == "Mix_A_x_1");
  DREAMOR_CHECK(std::string{dreamor::indexedName("Step", cStep{-2}, cPeriod{7})} == "Step_-2_7");

  // a lookup takes the columns, and a group by a column slices by it
  DREAMOR_CHECK(sPERIOD.membership()(cPeriod{2}) && !sPERIOD.membership()(cPeriod{9}));
  const auto byStart = sSTART.groupBy<cStart>();
  DREAMOR_CHECK(byStart.slice(cStart{2}).size() == 2);

  // a text column shows the strings of a source that outlives the set: a named container, a view of one,
  // or the reference a lateral source returns
  const std::vector<std::string> tools{"anvil", "bolt"};
  const std::vector<std::vector<std::string>> tools_of{{"cog"}, {"dial", "edge"}};
  const auto tools_at = [&tools_of](cStart s) -> const std::vector<std::string>& { return tools_of[s]; };
  const auto sNAME    = dreamor::index_set::relational::from<cGrade>(tools).build();
  const auto sSPAN    = dreamor::index_set::relational::from<cGrade>(std::span<const std::string>{tools}).build();
  const auto sOF =
      dreamor::index_set::relational::from<cStart>(std::vector<std::uint32_t>{0, 1}).lateral<cGrade>(tools_at).build();
  DREAMOR_CHECK(sNAME.size() == 2 && sSPAN == sNAME && sOF.size() == 3 && std::get<cGrade>(*sNAME.begin()) == "anvil");

  return report("value_column");
} catch (...) { return ::uncaught(); }
