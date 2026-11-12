// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL was
// not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// FormatBuffer: format() is static, on a buffer of the thread's - a const member function names its
// rows with it, threads name theirs at once, and the name is valid until the thread's next format().
#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "dreamor/format_buffer.hpp"
#include "dreamor/index_set.hpp"
#include "support/check.hpp"

namespace {

DREAMOR_VALUE_COLUMN(cPeriod, std::size_t);

// A value whose formatter throws.
struct Throws {};

}  // namespace

template <>
struct std::formatter<Throws> {
  static constexpr auto parse(std::format_parse_context& context) { return context.begin(); }

  template <typename FormatContext>
  static auto format(const Throws& /*value*/, FormatContext& context) -> decltype(context.out()) {
    throw std::runtime_error("bad datum");
  }
};

namespace {

// A model names its rows from const member functions: format() changes no object.
class Rows : private dreamor::FormatBuffer<32> {
 public:
  explicit Rows(std::string_view name) : family{name} {}

  [[nodiscard]] std::string capacity(std::size_t m, cPeriod t) const { return format("{}_{}_{}", family, m, t); }

  [[nodiscard]] const char* raw(std::size_t m) const { return format("{}_{}", family, m); }

  [[nodiscard]] std::string tooLong() const { return format("{}{:>40}", family, 1); }

 private:
  std::string_view family;
};

class QuietRows : private dreamor::FormatBuffer<8, dreamor::OnFormatOverflow::kLogAndClear> {
 public:
  explicit QuietRows(std::string_view name) : family{name} {}

  [[nodiscard]] std::string tooLong() const noexcept { return format("{}_{}", family, 123456789); }

  [[nodiscard]] std::string thrown() const noexcept { return format("{}_{}", family, Throws{}); }

 private:
  std::string_view family;
};

// A buffer of 8: names of up to 7 characters.
struct Tiny : private dreamor::FormatBuffer<8> {
  [[nodiscard]] static std::string of(std::string_view text) { return format("{}", text); }
};

// The buffer is no member: a FormatBuffer adds nothing to the object.
struct Sized : private dreamor::FormatBuffer<256> {
  int value = 0;
};

static_assert(sizeof(Sized) == sizeof(int));

constexpr std::size_t kThreads = 8;
constexpr std::size_t kNames   = 2000;

// Each thread names its own rows while the others name theirs; every name comes back whole.
void nameAlone(std::size_t thread, char& whole) {
  const Rows rows{"Cap"};
  whole = 1;
  for (std::size_t k = 0; k < kNames; ++k) {
    if (rows.capacity(thread, cPeriod{k}) != std::format("Cap_{}_{}", thread, k)) { whole = 0; }
  }
}

// append(value) writes what std::to_chars writes, and returns the name's length.
template <typename T>
bool writesAsToChars(T value) {
  dreamor::NameBuilder<64> names{"x"};
  std::array<char, 32> expected{};
  char* const end = std::to_chars(expected.data(), expected.data() + expected.size(), value).ptr;
  const std::string text{expected.data(), end};
  return names.append(names.rootLength(), value) == 2 + text.size() && names.name() == "x_" + text;
}

// Every digit count of T: 0, min, max, each power of ten and its neighbours, of both signs.
template <typename T>
bool writesEveryWidth() {
  using Limits = std::numeric_limits<T>;
  bool same    = writesAsToChars(T{0}) && writesAsToChars(Limits::min()) && writesAsToChars(Limits::max());
  for (T power = 1;; power = static_cast<T>(power * 10)) {
    same = same && writesAsToChars(static_cast<T>(power - 1)) && writesAsToChars(power) &&
           writesAsToChars(static_cast<T>(power + 1));
    if constexpr (Limits::is_signed) {
      same = same && writesAsToChars(static_cast<T>(-power)) && writesAsToChars(static_cast<T>(-power - 1)) &&
             writesAsToChars(static_cast<T>(-power + 1));
    }
    if (power > Limits::max() / 10) { break; }
  }
  return same;
}

}  // namespace

int main() try {
  const Rows rows{"Cap"};
  DREAMOR_CHECK(rows.capacity(3, cPeriod{2}) == "Cap_3_2");

  // Too long a name: kThrow throws, kLogAndClear gives an empty one.
  DREAMOR_CHECK_THROWS(std::runtime_error, rows.tooLong());
  DREAMOR_CHECK(QuietRows{"Quiet"}.tooLong().empty());
  // A formatter that throws: kLogAndClear gives an empty name, kThrow lets the exception through append().
  DREAMOR_CHECK(QuietRows{"Quiet"}.thrown().empty());
  dreamor::NameBuilder<32> throwing{"Bad"};
  DREAMOR_CHECK_THROWS(std::runtime_error, static_cast<void>(throwing.append(throwing.rootLength(), Throws{})));

  // N - 1 characters fit, with the terminator; one more is too long - through format, text and a number alike.
  DREAMOR_CHECK(Tiny::of("1234567") == "1234567");
  DREAMOR_CHECK_THROWS(std::runtime_error, static_cast<void>(Tiny::of("12345678")));
  dreamor::NameBuilder<8> fits{"abc"};
  DREAMOR_CHECK(fits.append(fits.rootLength(), std::string_view{"def"}) == 7 && std::string{fits.name()} == "abc_def");
  DREAMOR_CHECK(fits.append(fits.rootLength(), 123) == 7 && std::string{fits.name()} == "abc_123");
  dreamor::NameBuilder<8> over{"abcd"};
  DREAMOR_CHECK_THROWS(std::runtime_error, static_cast<void>(over.append(over.rootLength(), std::string_view{"efg"})));
  DREAMOR_CHECK_THROWS(std::runtime_error, static_cast<void>(over.append(over.rootLength(), 123)));

  // a text of every length from 0 to 40 copies whole, and a multi-character separator too; under kLogAndClear, a
  // text past the end is cut at N - 1 characters
  dreamor::NameBuilder<64> lengths{"R"};
  for (std::size_t n = 0; n <= 40; ++n) {
    const std::string text(n, 'x');
    DREAMOR_CHECK(lengths.append(lengths.rootLength(), text) == 2 + n && lengths.name() == "R_" + text);
    DREAMOR_CHECK(lengths.append(lengths.rootLength(), text, "->") == 3 + n && lengths.name() == "R->" + text);
  }
  dreamor::NameBuilder<16, dreamor::OnFormatOverflow::kLogAndClear> cut{"R"};
  DREAMOR_CHECK(cut.append(cut.rootLength(), std::string(20, 'x')) == 15 && cut.name() == "R_" + std::string(13, 'x'));

  // a number as std::to_chars writes it, in every integer type, char too
  DREAMOR_CHECK(writesEveryWidth<signed char>() && writesEveryWidth<unsigned char>() && writesEveryWidth<char>());
  DREAMOR_CHECK(writesEveryWidth<short>() && writesEveryWidth<unsigned short>());
  DREAMOR_CHECK(writesEveryWidth<int>() && writesEveryWidth<unsigned>());
  DREAMOR_CHECK(writesEveryWidth<long>() && writesEveryWidth<unsigned long>());
  DREAMOR_CHECK(writesEveryWidth<long long>() && writesEveryWidth<unsigned long long>());
  // near the end, where the widest number of the type may not fit: the checked path, the same name, and an overflow
  // that throws
  dreamor::NameBuilder<16> nearEnd{"abcdef"};
  DREAMOR_CHECK(nearEnd.append(nearEnd.rootLength(), -1234567) == 15 &&
                nearEnd.name() == std::string{"abcdef_-1234567"});
  DREAMOR_CHECK_THROWS(std::runtime_error, static_cast<void>(nearEnd.append(nearEnd.rootLength(), -12345678)));

  // A root too long for restart throws before the builder changes: its name, its root and whether it is enabled.
  dreamor::NameBuilder<8> kept{"abc"};
  DREAMOR_CHECK(kept.append(kept.rootLength(), 1) == 5);
  DREAMOR_CHECK_THROWS(std::runtime_error, static_cast<void>(kept.restart("too long a root")));
  DREAMOR_CHECK(kept.rootLength() == 3 && std::string{kept.name()} == "abc_1");
  dreamor::NameBuilder<8> off{nullptr};
  DREAMOR_CHECK_THROWS(std::runtime_error, static_cast<void>(off.restart("too long a root")));
  DREAMOR_CHECK(!off.enabled() && off.name() == nullptr);

  // Valid until the thread's next format() - from any object of the same FormatBuffer<N, Policy>.
  const Rows other{"Other"};
  const char* first = rows.raw(1);
  DREAMOR_CHECK(std::string{first} == "Cap_1");
  const char* second = other.raw(2);
  DREAMOR_CHECK(first == second);
  DREAMOR_CHECK(std::string{first} == "Other_2");

  std::vector<char> whole(kThreads, 0);
  {
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (std::size_t t = 0; t < kThreads; ++t) {
      threads.emplace_back(nameAlone, t, std::ref(whole[t]));
    }
    for (std::thread& thread : threads) {
      thread.join();
    }
  }
  for (const char w : whole) {
    DREAMOR_CHECK(w == 1);
  }

  return report("format_buffer");
} catch (...) { return ::uncaught(); }
