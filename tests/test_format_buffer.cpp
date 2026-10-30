// SPDX-License-Identifier: Apache-2.0
// FormatBuffer: format() is static, on a buffer of the thread's - a const member function names its
// rows with it, threads name theirs at once, and the name is valid until the thread's next format().
#include <cstddef>
#include <format>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "check.hpp"
#include "dreamor/general/format_buffer.hpp"
#include "dreamor/index_set.hpp"

namespace {

using PeriodCol = dreamor::Col<std::size_t, "period">;

// A model names its rows from const member functions: format() changes no object.
class Rows : private dreamor::general::FormatBuffer<32> {
 public:
  explicit Rows(std::string_view name) : family{name} {}
  [[nodiscard]] std::string capacity(std::size_t m, PeriodCol t) const { return format("{}_{}_{}", family, m, t); }
  [[nodiscard]] const char* raw(std::size_t m) const { return format("{}_{}", family, m); }
  [[nodiscard]] std::string tooLong() const { return format("{}{:>40}", family, 1); }

 private:
  std::string_view family;
};

class QuietRows : private dreamor::general::FormatBuffer<8, dreamor::general::OnFormatOverflow::kLogAndClear> {
 public:
  explicit QuietRows(std::string_view name) : family{name} {}
  [[nodiscard]] std::string tooLong() const noexcept { return format("{}_{}", family, 123456789); }

 private:
  std::string_view family;
};

// The buffer is no member: a FormatBuffer adds nothing to the object.
struct Sized : private dreamor::general::FormatBuffer<256> {
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
    if (rows.capacity(thread, PeriodCol{k}) != std::format("Cap_{}_{}", thread, k)) { whole = 0; }
  }
}

}  // namespace

int main() try {
  const Rows rows{"Cap"};
  DREAMOR_CHECK(rows.capacity(3, PeriodCol{2}) == "Cap_3_2");

  // Too long a name: kThrow throws, kLogAndClear gives an empty one.
  DREAMOR_CHECK_THROWS(std::runtime_error, rows.tooLong());
  DREAMOR_CHECK(QuietRows{"Quiet"}.tooLong().empty());

  // Valid until the thread's next format() - from any object of the same FormatBuffer<N, Policy>.
  const Rows other{"Other"};
  const char* first = rows.raw(1);
  DREAMOR_CHECK(std::string{first} == "Cap_1");
  const char* second = other.raw(2);
  DREAMOR_CHECK(first == second);
  DREAMOR_CHECK(std::string{first} == "Other_2");

  std::vector<char> whole(kThreads, 0);
  {
    std::vector<std::jthread> threads;
    threads.reserve(kThreads);
    for (std::size_t t = 0; t < kThreads; ++t) {
      threads.emplace_back(nameAlone, t, std::ref(whole[t]));
    }
  }
  for (const char w : whole) {
    DREAMOR_CHECK(w == 1);
  }

  return report("format_buffer");
} catch (...) { return ::uncaught(); }
